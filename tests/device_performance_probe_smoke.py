"""Exercise the probe's actual telemetry health section without audio hardware."""
import subprocess
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'scripts/device-performance-remote.sh'


def health(start, end):
    source = SOURCE.read_text()
    parser = source[source.index('telemetry_metric() {'):source.index('temperature_milli_c() {')]
    section = source[source.index('telemetry_failures=0\n'):source.index('\necho "Scheduling checks"')]
    script = 'set -eu\ntmp_dir=$1\ntelemetry_ready=1\n' + parser + section
    script += '\nexit "$telemetry_failures"\n'
    with tempfile.TemporaryDirectory() as root:
        directory = Path(root)
        for label, counters in [('start', start), ('end', end)]:
            (directory / ('telemetry.' + label)).write_text(
                ' '.join(f'{key}={value}' for key, value in counters.items()) + '\n')
        return subprocess.run(['sh', '-s', '--', root], input=script,
                              capture_output=True, text=True)


class TelemetryHealth(unittest.TestCase):
    def setUp(self):
        self.start = dict(callbacks=100, over=0, gaps=0, worker_over=0,
                          worker_underflow=2, worker_submit_miss=0,
                          nonfinite=0, block_mismatch=0, bypassed=0,
                          avg='0.56ms', max='0.90ms', budget='1.33ms')
        self.end = dict(self.start, callbacks=200)

    def test_no_new_faults_including_startup_underflows(self):
        result = health(self.start, self.end)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('AUDIO HEALTH: PASS', result.stdout)
        self.assertIn('underflow=0 submit_miss=0', result.stdout)

    def test_worker_underflow_fails_health(self):
        self.end['worker_underflow'] = 11
        result = health(self.start, self.end)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('underflow=9', result.stdout)
        self.assertIn('AUDIO HEALTH: FAIL', result.stdout)

    def test_submission_miss_fails_health(self):
        self.end['worker_submit_miss'] = 1
        result = health(self.start, self.end)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('submit_miss=1', result.stdout)

    def test_worker_counter_reset_invalidates_sample(self):
        self.end['worker_underflow'] = 1
        result = health(self.start, self.end)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('INVALID: telemetry counters reset', result.stdout)

    def test_callback_overrun_still_fails(self):
        self.end['over'] = 1
        result = health(self.start, self.end)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('callback_over=1', result.stdout)

    def test_older_telemetry_without_worker_fields_remains_usable(self):
        for counters in [self.start, self.end]:
            del counters['worker_underflow']
            del counters['worker_submit_miss']
        result = health(self.start, self.end)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
