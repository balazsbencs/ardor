#!/usr/bin/env python3
"""Measure Ardor jack-to-jack latency with an audio interface and a Y cable.

Wiring: interface output 1 -> Y cable. One leg goes to interface input 1 (the
reference), the other to the Ardor guitar input. The Ardor output goes to
interface input 2. The interface adds the same delay to both inputs, so the
time between the two recorded clicks is the Ardor latency alone. Playback and
recording need no sync. See docs/latency-measurement.md.

Standard library only. `run` needs macOS, ffmpeg and afplay; `generate` and
`analyze` work with any DAW.
"""

import argparse
import math
import random
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time
from array import array
from dataclasses import dataclass, field
from pathlib import Path

GAP_RANGE_S = (0.25, 0.45)
LEAD_IN_S = 2.0
TAIL_S = 1.0
CAPTURE_START_S = 1.0
SILENCE_DBFS = -70.0
MISSED_RELATIVE = 0.05
CLIP_LEVEL = 0.99
SPREAD_WARN_MS = 0.1
DISAGREE_WARN_MS = 0.25
FORMAT_PCM = 1
FORMAT_FLOAT = 3
FORMAT_EXTENSIBLE = 0xFFFE
MONITOR_WARNING = (
    "Turn direct monitoring and software playthrough OFF on the interface.\n"
    "Otherwise input 1 hears output 1 twice and the Ardor output feeds back into its input."
)


class AnalysisError(Exception):
    """A recording or a file that cannot give a latency value."""


@dataclass(frozen=True)
class Result:
    sample_rate: int
    found: int
    measured: int
    missed: int
    xcorr_ms: list
    onset_ms: list
    polarity: int
    ref_peak_db: float
    dut_peak_db: float
    warnings: list = field(default_factory=list)

    @property
    def median_xcorr_ms(self):
        return statistics.median(self.xcorr_ms)

    @property
    def median_onset_ms(self):
        return statistics.median(self.onset_ms)


def to_frames(ms, rate):
    return int(round(ms * rate / 1000.0))


def to_db(level):
    return 20.0 * math.log10(level) if level > 0 else float("-inf")


def generate_clicks(sample_rate, count, level_db, seed, lead_in_s=LEAD_IN_S, tail_s=TAIL_S):
    """Return (samples, onsets): Hann clicks with random gaps, so no period repeats."""
    rng = random.Random(seed)
    amplitude = 10.0 ** (level_db / 20.0)
    width = 2 * round(sample_rate / 6000) + 1
    click = [amplitude * (0.5 - 0.5 * math.cos(2 * math.pi * k / (width - 1))) for k in range(width)]
    onsets = []
    position = int(lead_in_s * sample_rate)
    for _ in range(count):
        onsets.append(position)
        position += int(rng.uniform(*GAP_RANGE_S) * sample_rate)
    samples = [0.0] * (onsets[-1] + width + int(tail_s * sample_rate))
    for onset in onsets:
        samples[onset:onset + width] = click
    return samples, onsets


def find_clicks(ref, rate):
    """Return the peak index of each reference click."""
    threshold = 0.5 * max(abs(x) for x in ref)
    peaks = []
    i = 0
    while i < len(ref):
        if abs(ref[i]) >= threshold:
            end = min(len(ref), i + to_frames(2.0, rate))
            peaks.append(max(range(i, end), key=lambda n: abs(ref[n])))
            i += to_frames(200.0, rate)
        else:
            i += 1
    return peaks


def onset_before(signal, peak, lowest):
    """Walk back from a peak to the first sample at or above half its level."""
    threshold = 0.5 * abs(signal[peak])
    i = peak
    while i > lowest and abs(signal[i - 1]) >= threshold:
        i -= 1
    return i


def xcorr_lag(ref, dut, ref_peak, centre, rate):
    """Return (lag in frames with sub-frame precision, sign) near a coarse lag."""
    start = max(0, ref_peak - to_frames(1.0, rate))
    stop = min(len(ref), ref_peak + to_frames(4.0, rate))
    reach = to_frames(1.5, rate)
    segment = [(n, ref[n]) for n in range(start, stop) if ref[n]]

    def corr(lag):
        return sum(x * dut[n + lag] for n, x in segment if 0 <= n + lag < len(dut))

    lags = range(centre - reach, centre + reach + 1)
    values = {lag: corr(lag) for lag in range(lags.start - 1, lags.stop + 1)}
    best = max(lags, key=lambda lag: abs(values[lag]))
    a, b, c = (abs(values[best - 1]), abs(values[best]), abs(values[best + 1]))
    bend = a - 2 * b + c
    offset = 0.5 * (a - c) / bend if bend else 0.0
    return best + offset, (1 if values[best] >= 0 else -1)


def measure_click(ref, dut, ref_peak, rate, max_latency_ms, floor):
    """Return (xcorr frames, onset frames, sign) for one click, or None if missed."""
    low = max(0, ref_peak - to_frames(1.0, rate))
    high = min(len(dut), ref_peak + to_frames(max_latency_ms, rate))
    if high <= low:
        return None
    window_peak = max(abs(dut[n]) for n in range(low, high))
    if window_peak < floor:
        return None
    # The earliest strong arrival: a delay repeat is weaker than the direct sound.
    coarse = next(n for n in range(low, high) if abs(dut[n]) >= 0.9 * window_peak)
    local_end = min(len(dut), coarse + to_frames(1.0, rate))
    dut_peak = max(range(coarse, local_end), key=lambda n: abs(dut[n]))
    lag, sign = xcorr_lag(ref, dut, ref_peak, dut_peak - ref_peak, rate)
    ref_onset = onset_before(ref, ref_peak, ref_peak - to_frames(2.0, rate))
    dut_onset = onset_before(dut, dut_peak, dut_peak - to_frames(2.0, rate))
    return lag, dut_onset - ref_onset, sign


def collect_warnings(missed, ref_peak, dut_peak, xcorr_ms, onset_ms, signs):
    warnings = []
    if missed:
        warnings.append(f"{missed} clicks had no output on the Ardor channel "
                        "(noise gate closed, or the level is too low)")
    if ref_peak >= CLIP_LEVEL:
        warnings.append("the reference channel clips: lower the interface output")
    if dut_peak >= CLIP_LEVEL:
        warnings.append("the Ardor channel clips at the interface input: lower the Ardor output")
    spread = max(xcorr_ms) - min(xcorr_ms)
    if spread > SPREAD_WARN_MS:
        warnings.append(f"the latency changes by {spread:.3f} ms between clicks")
    difference = statistics.median(onset_ms) - statistics.median(xcorr_ms)
    if abs(difference) > DISAGREE_WARN_MS:
        warnings.append(f"the two methods disagree by {difference:+.3f} ms: the path filters or "
                        "distorts the click. Use cross-correlation for the declared-latency "
                        "check and onset for feel")
    if len(set(signs)) > 1:
        warnings.append("the polarity changes between clicks")
    return warnings


def analyze(ref, dut, sample_rate, max_latency_ms=50.0):
    """Measure the delay of dut against ref for each click in ref."""
    ref_peak = max((abs(x) for x in ref), default=0.0)
    if to_db(ref_peak) < SILENCE_DBFS:
        raise AnalysisError("no clicks found in the reference channel (it is silent)")
    dut_peak = max((abs(x) for x in dut), default=0.0)
    floor = max(10.0 ** (SILENCE_DBFS / 20.0), MISSED_RELATIVE * dut_peak)
    peaks = find_clicks(ref, sample_rate)
    found = [measure_click(ref, dut, p, sample_rate, max_latency_ms, floor) for p in peaks]
    measured = [m for m in found if m is not None]
    if not measured:
        raise AnalysisError(f"found {len(peaks)} reference clicks but no output on the Ardor channel")
    xcorr_ms = [lag * 1000.0 / sample_rate for lag, _, _ in measured]
    onset_ms = [onset * 1000.0 / sample_rate for _, onset, _ in measured]
    signs = [sign for _, _, sign in measured]
    missed = len(found) - len(measured)
    return Result(
        sample_rate=sample_rate, found=len(peaks), measured=len(measured), missed=missed,
        xcorr_ms=xcorr_ms, onset_ms=onset_ms,
        polarity=1 if signs.count(1) >= signs.count(-1) else -1,
        ref_peak_db=to_db(ref_peak), dut_peak_db=to_db(dut_peak),
        warnings=collect_warnings(missed, ref_peak, dut_peak, xcorr_ms, onset_ms, signs),
    )


def format_line(name, values, rate):
    median = statistics.median(values)
    return (f"latency ({name}): median {median:.3f} ms "
            f"({median * rate / 1000.0:.1f} frames at {rate} Hz), "
            f"min {min(values):.3f} ms, max {max(values):.3f} ms")


def format_report(result):
    lines = [
        f"clicks: {result.found} found, {result.measured} measured, {result.missed} missed",
        f"levels: reference peak {result.ref_peak_db:.1f} dBFS, "
        f"Ardor peak {result.dut_peak_db:.1f} dBFS",
        format_line("cross-correlation", result.xcorr_ms, result.sample_rate),
        format_line("onset", result.onset_ms, result.sample_rate),
        "polarity: " + ("normal" if result.polarity > 0 else "inverted"),
    ]
    return "\n".join(lines + [f"warning: {w}" for w in result.warnings])


def parse_chunks(data):
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise AnalysisError("not a RIFF/WAVE file")
    chunks = {}
    position = 12
    while position + 8 <= len(data):
        name = data[position:position + 4]
        size = struct.unpack_from("<I", data, position + 4)[0]
        chunks.setdefault(name, data[position + 8:position + 8 + size])
        position += 8 + size + (size & 1)
    if b"fmt " not in chunks or b"data" not in chunks:
        raise AnalysisError("the WAV file has no fmt or data chunk")
    return chunks


def decode_samples(payload, kind, bits):
    if kind == FORMAT_FLOAT and bits in (32, 64):
        values = array("f" if bits == 32 else "d", payload)
        scale = 1.0
    elif kind == FORMAT_PCM and bits == 16:
        values, scale = array("h", payload), 32768.0
    elif kind == FORMAT_PCM and bits == 32:
        values, scale = array("i", payload), 2147483648.0
    elif kind == FORMAT_PCM and bits == 24:
        count = len(payload) // 3
        wide = bytearray(count * 4)
        for byte in range(3):
            wide[byte + 1::4] = payload[byte:count * 3:3]
        values, scale = array("i", bytes(wide)), 2147483648.0
    else:
        raise AnalysisError(f"unsupported WAV sample format {kind} with {bits} bits")
    if sys.byteorder == "big":
        values.byteswap()
    return [v / scale for v in values]


def read_wav(path):
    """Return (sample_rate, channels) for PCM 16/24/32-bit or float WAV, extensible too."""
    chunks = parse_chunks(Path(path).read_bytes())
    fmt = chunks[b"fmt "]
    if len(fmt) < 16:
        raise AnalysisError("the WAV fmt chunk is too short")
    kind, count, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
    if kind == FORMAT_EXTENSIBLE:
        if len(fmt) < 26:
            raise AnalysisError("the extensible WAV fmt chunk is too short")
        kind = struct.unpack_from("<H", fmt, 24)[0]
    payload = chunks[b"data"]
    payload = payload[:len(payload) - len(payload) % align] if align else payload
    samples = decode_samples(payload, kind, bits)
    return rate, [samples[c::count] for c in range(count)]


def write_wav(path, channels, sample_rate):
    """Write 24-bit PCM, interleaving the given channels."""
    frames = len(channels[0])
    count = len(channels)
    ints = array("i", (max(-(1 << 23), min((1 << 23) - 1, round(channels[c][i] * (1 << 23)))) << 8
                       for i in range(frames) for c in range(count)))
    if sys.byteorder == "big":
        ints.byteswap()
    wide = ints.tobytes()
    packed = bytearray(len(wide) // 4 * 3)
    for byte in range(3):
        packed[byte::3] = wide[byte + 1::4]
    fmt = struct.pack("<HHIIHH", FORMAT_PCM, count, sample_rate, sample_rate * 3 * count, 3 * count, 24)
    body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"data" + struct.pack("<I", len(packed)) + bytes(packed) + b"\0" * (len(packed) & 1))
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def analyze_file(path, ref_channel, dut_channel, max_latency_ms):
    rate, channels = read_wav(path)
    for channel in (ref_channel, dut_channel):
        if not 1 <= channel <= len(channels):
            raise AnalysisError(f"channel {channel} is not in the file ({len(channels)} channels)")
    return analyze(channels[ref_channel - 1], channels[dut_channel - 1], rate, max_latency_ms)


def command_generate(args):
    samples, _ = generate_clicks(args.sample_rate, args.count, args.level_db, args.seed)
    write_wav(args.output, [samples, samples], args.sample_rate)
    print(f"wrote {args.output}: {args.count} clicks at {args.level_db:.1f} dBFS, "
          f"{len(samples) / args.sample_rate:.1f} s, play it on interface output 1")


def command_analyze(args):
    result = analyze_file(args.recording, args.ref_channel, args.dut_channel, args.max_latency_ms)
    print(format_report(result))
    if args.verbose:
        for n, (x, o) in enumerate(zip(result.xcorr_ms, result.onset_ms), 1):
            print(f"click {n:3d}: cross-correlation {x:.3f} ms, onset {o:.3f} ms")


def require_macos_tools():
    if sys.platform != "darwin":
        raise AnalysisError("run needs macOS; use generate and analyze with a DAW instead")
    for tool in ("ffmpeg", "afplay"):
        if shutil.which(tool) is None:
            raise AnalysisError(f"run needs {tool} on PATH")


def capture(test_wav, recording, device, duration):
    recorder = subprocess.Popen(
        ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation",
         "-i", f":{device}", "-t", f"{duration:.2f}", "-c:a", "pcm_s24le", str(recording)])
    try:
        time.sleep(CAPTURE_START_S)
        subprocess.run(["afplay", str(test_wav)], check=True)
        recorder.wait(timeout=duration + 10)
    finally:
        if recorder.poll() is None:
            recorder.kill()
    if recorder.returncode != 0:
        raise AnalysisError(f"ffmpeg stopped with code {recorder.returncode}; "
                            "check the device index with run --list-devices")


def command_run(args):
    require_macos_tools()
    if args.list_devices:
        subprocess.run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""])
        return
    if args.device is None:
        raise AnalysisError("give the interface with --device; find its audio index with run --list-devices")
    print(MONITOR_WARNING)
    print("afplay plays to the macOS default output: set the interface as the default output.")
    folder = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="ardor-latency-"))
    folder.mkdir(parents=True, exist_ok=True)
    test_wav, recording = folder / "clicks.wav", folder / "recording.wav"
    samples, _ = generate_clicks(args.sample_rate, args.count, args.level_db, args.seed)
    write_wav(test_wav, [samples, samples], args.sample_rate)
    capture(test_wav, recording, args.device, len(samples) / args.sample_rate + CAPTURE_START_S + 1.0)
    print(f"recording: {recording}")
    try:
        result = analyze_file(recording, args.ref_channel, args.dut_channel, args.max_latency_ms)
    except AnalysisError as error:
        raise AnalysisError(f"{error}. If the recording is silent, give the terminal microphone "
                            "permission (System Settings > Privacy & Security > Microphone), check "
                            "the device index and the default output") from error
    print(format_report(result))


def bounded(kind, low, high):
    def parse(text):
        value = kind(text)
        if not low <= value <= high:
            raise argparse.ArgumentTypeError(f"must be between {low} and {high}")
        return value
    return parse


def add_signal_options(parser):
    parser.add_argument("--count", type=bounded(int, 3, 1000), default=30)
    parser.add_argument("--level-db", type=bounded(float, -60.0, -1.0), default=-20.0,
                        help="click peak in dBFS (default -20, near a guitar)")
    parser.add_argument("--sample-rate", type=bounded(int, 8000, 192000), default=48000)
    parser.add_argument("--seed", type=int, default=1)


def add_analysis_options(parser):
    parser.add_argument("--ref-channel", type=bounded(int, 1, 64), default=1)
    parser.add_argument("--dut-channel", type=bounded(int, 1, 64), default=2)
    parser.add_argument("--max-latency-ms", type=bounded(float, 1.0, 500.0), default=50.0)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate", help="write the click WAV for a DAW")
    generate.add_argument("output", type=Path)
    add_signal_options(generate)
    analyze_cmd = commands.add_parser("analyze", help="analyze a recording with both inputs")
    analyze_cmd.add_argument("recording", type=Path)
    analyze_cmd.add_argument("--verbose", action="store_true", help="print each click")
    add_analysis_options(analyze_cmd)
    run = commands.add_parser("run", help="play, record and analyze (macOS, ffmpeg)")
    run.add_argument("--device", type=bounded(int, 0, 255), help="avfoundation audio index (required)")
    run.add_argument("--list-devices", action="store_true")
    run.add_argument("--keep", type=Path, help="folder for clicks.wav and recording.wav")
    add_signal_options(run)
    add_analysis_options(run)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    handlers = {"generate": command_generate, "analyze": command_analyze, "run": command_run}
    try:
        handlers[args.command](args)
    except (AnalysisError, OSError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
