"""Exercise the LAN installer's actual environment update without deploying."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "scripts/deploy-lan.sh").read_text()
UPDATE = SCRIPT.split('if [ "$local_auth" != "preserve" ]; then\n', 1)[1].split("\nsync\n", 1)[0]
UPDATE = 'if [ "$local_auth" != "preserve" ]; then\n' + UPDATE
DEFAULT_ID = next(
    line.split("=", 1)[1]
    for line in (ROOT / "buildroot/external/board/ardor-pedal/rootfs-overlay/etc/ardor-managerd.env").read_text().splitlines()
    if line.startswith("TONE3000_CLIENT_ID=")
)


class DeployLanEnvironmentTest(unittest.TestCase):
    def update(self, previous=None, client_id="__ARDOR_TONE3000_CLIENT_ID_UNSET__", auth="on"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "managerd.env"
            if previous is not None:
                path.write_text(previous)
            subprocess.run(["sh", "-eu", "-c", UPDATE], check=True, env={
                **os.environ,
                "managerd_env": str(path),
                "local_auth": auth,
                "tone3000_client_id": client_id,
                "tone3000_default_client_id": DEFAULT_ID,
                "tone3000_base_url": "https://www.tone3000.com",
            })
            return path.read_text()

    def test_preserves_custom_integration_and_updates_auth(self):
        result = self.update("ARDOR_API_AUTH=off\nARDOR_API_TOKEN=old\nOTHER=keep\n"
                             "TONE3000_CLIENT_ID=custom\nTONE3000_BASE_URL=https://custom.example\n")
        self.assertIn("ARDOR_API_AUTH=on\n", result)
        self.assertNotIn("ARDOR_API_TOKEN", result)
        self.assertIn("OTHER=keep\n", result)
        self.assertIn("TONE3000_CLIENT_ID=custom\n", result)
        self.assertIn("TONE3000_BASE_URL=https://custom.example\n", result)

    def test_repairs_missing_or_empty_client_id(self):
        for previous in (None, "ARDOR_API_AUTH=on\n", "TONE3000_CLIENT_ID=\n"):
            with self.subTest(previous=previous):
                result = self.update(previous)
                self.assertIn(f"TONE3000_CLIENT_ID={DEFAULT_ID}\n", result)
                self.assertEqual(result.count("TONE3000_CLIENT_ID="), 1)

    def test_explicit_override_replaces_integration(self):
        result = self.update("TONE3000_CLIENT_ID=old\nTONE3000_BASE_URL=https://old.example\n", "new")
        self.assertIn("TONE3000_CLIENT_ID=new\n", result)
        self.assertIn("TONE3000_BASE_URL=https://www.tone3000.com\n", result)
        self.assertEqual(result.count("TONE3000_CLIENT_ID="), 1)
        self.assertEqual(result.count("TONE3000_BASE_URL="), 1)

    def test_preserve_mode_leaves_environment_untouched(self):
        previous = "ARDOR_API_AUTH=off\nTONE3000_CLIENT_ID=custom\n"
        self.assertEqual(self.update(previous, auth="preserve"), previous)


if __name__ == "__main__":
    unittest.main()
