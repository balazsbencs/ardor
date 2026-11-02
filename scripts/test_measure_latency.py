#!/usr/bin/env python3
"""Tests for measure-latency.py. Run: python3 -I scripts/test_measure_latency.py"""

import contextlib
import importlib.util
import io
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).with_name("measure-latency.py")
spec = importlib.util.spec_from_file_location("measure_latency", SCRIPT)
ml = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ml)

RATE = 48000


def delayed(signal, frames, gain=1.0):
    return [0.0] * frames + [gain * x for x in signal[: len(signal) - frames]]


def fir(signal, taps):
    # Scatter only the non-zero input samples; the click signals are mostly silence.
    half = len(taps) // 2
    out = [0.0] * len(signal)
    for i, x in enumerate(signal):
        if x:
            for k, t in enumerate(taps):
                j = i + k - half
                if 0 <= j < len(out):
                    out[j] += t * x
    return out


def hann_taps(count):
    import math
    raw = [0.5 - 0.5 * math.cos(2 * math.pi * (k + 1) / (count + 1)) for k in range(count)]
    total = sum(raw)
    return [t / total for t in raw]


def with_noise(signal, level, seed=7):
    rng = random.Random(seed)
    return [x + rng.uniform(-level, level) for x in signal]


def frames_to_ms(frames, rate=RATE):
    return frames * 1000.0 / rate


class GenerateTests(unittest.TestCase):
    def test_gaps_are_random_and_inside_range(self):
        samples, onsets = ml.generate_clicks(RATE, count=30, level_db=-20.0, seed=1)
        self.assertEqual(len(onsets), 30)
        gaps = [b - a for a, b in zip(onsets, onsets[1:])]
        self.assertTrue(all(0.25 * RATE <= g <= 0.45 * RATE for g in gaps))
        self.assertGreater(len(set(gaps)), 20)
        self.assertAlmostEqual(max(abs(x) for x in samples), 0.1, places=3)

    def test_same_seed_gives_same_signal(self):
        self.assertEqual(ml.generate_clicks(RATE, 10, -20.0, seed=3),
                         ml.generate_clicks(RATE, 10, -20.0, seed=3))


class AnalyzeTests(unittest.TestCase):
    def setUp(self):
        self.ref, _ = ml.generate_clicks(RATE, count=20, level_db=-20.0, seed=2)

    def test_integer_delay(self):
        dut = with_noise(delayed(self.ref, 257, gain=0.3), 1e-4)
        result = ml.analyze(with_noise(self.ref, 1e-4, seed=8), dut, RATE)
        self.assertEqual(result.measured, 20)
        self.assertEqual(result.missed, 0)
        self.assertAlmostEqual(result.median_xcorr_ms, frames_to_ms(257), delta=0.01)
        self.assertAlmostEqual(result.median_onset_ms, frames_to_ms(257), delta=frames_to_ms(1))
        self.assertEqual(result.polarity, 1)
        self.assertEqual(result.warnings, [])

    def test_sub_sample_delay(self):
        # fir() places tap 1 at offset 0, so a two-tap average moves the click half a frame earlier.
        dut = fir(delayed(self.ref, 100), [0.5, 0.5])
        result = ml.analyze(self.ref, dut, RATE)
        self.assertAlmostEqual(result.median_xcorr_ms, frames_to_ms(99.5), delta=frames_to_ms(0.15))

    def test_inverted_polarity(self):
        result = ml.analyze(self.ref, delayed(self.ref, 300, gain=-0.5), RATE)
        self.assertEqual(result.polarity, -1)
        self.assertAlmostEqual(result.median_xcorr_ms, frames_to_ms(300), delta=0.01)

    def test_delay_repeats_do_not_move_the_result(self):
        direct = delayed(self.ref, 257)
        echo = delayed(self.ref, 257 + int(0.3 * RATE), gain=0.6)
        result = ml.analyze(self.ref, [a + b for a, b in zip(direct, echo)], RATE)
        self.assertAlmostEqual(result.median_xcorr_ms, frames_to_ms(257), delta=0.01)
        self.assertAlmostEqual(result.median_onset_ms, frames_to_ms(257), delta=frames_to_ms(1))

    def test_silent_clicks_are_counted_as_missed(self):
        dut = delayed(self.ref, 257)
        _, onsets = ml.generate_clicks(RATE, count=20, level_db=-20.0, seed=2)
        for onset in onsets[:3]:
            dut[onset: onset + 2000] = [0.0] * 2000
        result = ml.analyze(self.ref, dut, RATE)
        self.assertEqual(result.missed, 3)
        self.assertEqual(result.measured, 17)
        self.assertTrue(any("no output" in w for w in result.warnings))

    def test_symmetric_fir_reports_its_group_delay(self):
        dut = fir(delayed(self.ref, 100), hann_taps(129))
        result = ml.analyze(self.ref, [x * 20 for x in dut], RATE)
        self.assertAlmostEqual(result.median_xcorr_ms, frames_to_ms(100), delta=frames_to_ms(0.5))
        self.assertLess(result.median_onset_ms, result.median_xcorr_ms - 0.25)
        self.assertTrue(any("disagree" in w for w in result.warnings))

    def test_hard_clipped_output_onset(self):
        dut = [max(-0.5, min(0.5, 50 * x)) for x in delayed(self.ref, 200)]
        result = ml.analyze(self.ref, dut, RATE)
        self.assertAlmostEqual(result.median_onset_ms, frames_to_ms(200), delta=frames_to_ms(8))

    def test_other_sample_rate(self):
        ref, _ = ml.generate_clicks(44100, count=10, level_db=-20.0, seed=4)
        result = ml.analyze(ref, delayed(ref, 236), 44100)
        self.assertAlmostEqual(result.median_xcorr_ms, frames_to_ms(236, 44100), delta=0.01)

    def test_silent_reference_raises(self):
        with self.assertRaises(ml.AnalysisError) as caught:
            ml.analyze([0.0] * RATE, [0.0] * RATE, RATE)
        self.assertIn("reference", str(caught.exception))

    def test_clipping_is_reported(self):
        dut = [max(-1.0, min(1.0, 12 * x)) for x in delayed(self.ref, 257)]
        result = ml.analyze(self.ref, dut, RATE)
        self.assertTrue(any("clips" in w for w in result.warnings))

    def test_report_names_both_methods(self):
        report = ml.format_report(ml.analyze(self.ref, delayed(self.ref, 257), RATE))
        self.assertIn("cross-correlation", report)
        self.assertIn("onset", report)
        self.assertIn("5.354 ms", report)


def extensible_24bit_with_junk(path, channels, rate):
    frames = len(channels[0])
    data = bytearray()
    for i in range(frames):
        for channel in channels:
            value = max(-(1 << 23), min((1 << 23) - 1, round(channel[i] * (1 << 23))))
            data += (value & 0xFFFFFF).to_bytes(3, "little")
    guid = bytes.fromhex("0100000000001000800000aa00389b71")
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, len(channels), rate, rate * 3 * len(channels),
                      3 * len(channels), 24, 22, 24, 3) + guid
    junk = b"x" * 27
    body = (b"WAVE" + b"JUNK" + struct.pack("<I", len(junk)) + junk + b"\0"
            + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"LIST" + struct.pack("<I", 4) + b"INFO"
            + b"data" + struct.pack("<I", len(data)) + bytes(data))
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


class WavTests(unittest.TestCase):
    def test_reads_extensible_24bit_with_extra_chunks(self):
        left = [0.0, 0.5, -0.5, 0.25]
        right = [0.1, -0.1, 0.0, -1.0]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "ext.wav"
            extensible_24bit_with_junk(path, [left, right], 44100)
            rate, channels = ml.read_wav(path)
        self.assertEqual(rate, 44100)
        for expected, actual in zip([left, right], channels):
            for e, a in zip(expected, actual):
                self.assertAlmostEqual(e, a, delta=1e-6)

    def test_write_then_read_round_trip(self):
        signal = [0.0, 0.25, -0.75, 0.5]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "out.wav"
            ml.write_wav(path, [signal, signal], RATE)
            rate, channels = ml.read_wav(path)
        self.assertEqual(rate, RATE)
        self.assertEqual(len(channels), 2)
        for e, a in zip(signal, channels[1]):
            self.assertAlmostEqual(e, a, delta=1e-6)

    def test_reads_float32(self):
        signal = [0.0, 0.5, -0.25]
        fmt = struct.pack("<HHIIHH", 3, 1, RATE, RATE * 4, 4, 32)
        data = struct.pack("<3f", *signal)
        body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
                + b"data" + struct.pack("<I", len(data)) + data)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "f.wav"
            path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
            _, channels = ml.read_wav(path)
        self.assertEqual(channels[0], signal)

    def test_rejects_a_file_that_is_not_wav(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.wav"
            path.write_bytes(b"not a wav file at all")
            with self.assertRaises(ml.AnalysisError):
                ml.read_wav(path)


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = ml.main(argv)
    return code, out.getvalue(), err.getvalue()


class FakeRecorder:
    """Stands in for ffmpeg: writes a recording of clicks.wav with a delayed second channel."""

    def __init__(self, command, delay, gain):
        recording = Path(command[-1])
        rate, channels = ml.read_wav(recording.with_name("clicks.wav"))
        ref = channels[0]
        ml.write_wav(recording, [ref, delayed(ref, delay, gain)], rate)
        self.returncode = 0

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        return self.returncode

    def kill(self):
        pass


class CommandLineTests(unittest.TestCase):
    def run_capture(self, folder, delay, gain):
        with mock.patch.object(ml.sys, "platform", "darwin"), \
                mock.patch.object(ml.shutil, "which", return_value="/usr/bin/tool"), \
                mock.patch.object(ml.time, "sleep"), \
                mock.patch.object(ml.subprocess, "run") as player, \
                mock.patch.object(ml.subprocess, "Popen",
                                  side_effect=lambda command: FakeRecorder(command, delay, gain)) as recorder:
            result = run_main(["run", "--device", "2", "--count", "5", "--keep", folder])
        return result, player, recorder

    def test_run_plays_records_and_reports(self):
        with tempfile.TemporaryDirectory() as folder:
            (code, out, _), player, recorder = self.run_capture(folder, 257, 0.5)
            self.assertTrue((Path(folder) / "recording.wav").exists())
        self.assertEqual(code, 0)
        self.assertIn("direct monitoring", out)
        self.assertIn("5.354 ms", out)
        self.assertIn(":2", recorder.call_args.args[0])
        self.assertEqual(player.call_args.args[0][0], "afplay")

    def test_run_with_silent_recording_names_the_permission(self):
        with tempfile.TemporaryDirectory() as folder:
            (code, _, err), _, _ = self.run_capture(folder, 257, 0.0)
        self.assertEqual(code, 1)
        self.assertIn("microphone permission", err)

    def test_run_without_device_asks_for_one(self):
        with mock.patch.object(ml.sys, "platform", "darwin"), \
                mock.patch.object(ml.shutil, "which", return_value="/usr/bin/tool"), \
                mock.patch.object(ml.subprocess, "Popen") as recorder:
            code, _, err = run_main(["run"])
        self.assertEqual(code, 1)
        self.assertIn("--list-devices", err)
        recorder.assert_not_called()

    def test_generate_then_analyze_round_trip(self):
        with tempfile.TemporaryDirectory() as folder:
            clicks = str(Path(folder) / "clicks.wav")
            recording = Path(folder) / "recording.wav"
            self.assertEqual(run_main(["generate", clicks, "--count", "5"])[0], 0)
            rate, channels = ml.read_wav(clicks)
            ml.write_wav(recording, [channels[0], delayed(channels[0], 120, 0.5)], rate)
            code, out, _ = run_main(["analyze", str(recording)])
            bad_code, _, bad_err = run_main(["analyze", str(recording), "--dut-channel", "3"])
        self.assertEqual(code, 0)
        self.assertIn("2.500 ms", out)
        self.assertEqual(bad_code, 1)
        self.assertIn("channel 3", bad_err)


if __name__ == "__main__":
    unittest.main()
