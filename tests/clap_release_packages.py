#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the publication gate with complete, incomplete and damaged packages."""
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import plistlib
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("clap_packages", Path(__file__).resolve().parents[1] / "scripts/verify-clap-packages.py")
PACKAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGES)
VERSION = "0.1.9876"


class Packages(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.names = PACKAGES.package_names(VERSION)
        for name in self.names:
            self.archive(name)

    def archive(self, name, bundle_version=VERSION, include_module=True):
        nam = name.startswith("ArdorNAM-")
        root, module = ("ArdorNAM", "ArdorNAM") if nam else ("ardor-clap", "Ardor")
        notice = "licenses/CLAP.txt" if nam else "licenses/ardor-clap/CLAP.txt"
        files = {f"{root}/README.md": b"Packaging test fixture", f"{root}/{notice}": b"License test fixture"}
        binary_path = f"{root}/{module}.clap"
        if "macOS" in name:
            files[binary_path + "/Contents/Info.plist"] = plistlib.dumps({
                "CFBundleExecutable": module, "CFBundleVersion": bundle_version,
                "CFBundleShortVersionString": bundle_version,
            })
            binary_path += "/Contents/MacOS/" + module
        if include_module:
            files[binary_path] = b"Module test fixture"
        path = self.directory / name
        if name.endswith(".zip"):
            with zipfile.ZipFile(path, "w") as archive:
                for member, content in files.items():
                    archive.writestr(member, content)
        else:
            with tarfile.open(path, "w:gz") as archive:
                for member, content in files.items():
                    entry = tarfile.TarInfo(member)
                    entry.size = len(content)
                    archive.addfile(entry, io.BytesIO(content))
        self.checksum(name)

    def checksum(self, name, suffix="\n"):
        digest = hashlib.sha256((self.directory / name).read_bytes()).hexdigest()
        (self.directory / (name + ".sha256")).write_bytes(f"{digest}  {name}{suffix}".encode("ascii"))

    def verify(self, version=VERSION):
        with contextlib.redirect_stdout(io.StringIO()):
            return PACKAGES.verify(self.directory, version)

    def test_complete_set_and_windows_line_endings(self):
        self.checksum(self.names[1], suffix="\r\n")
        self.assertEqual(len(self.verify()), 10)

    def test_missing_nam_prevents_publication(self):
        (self.directory / self.names[-1]).unlink()
        with self.assertRaisesRegex(ValueError, "missing="):
            self.verify()

    def test_beta_or_unexpected_package_prevents_publication(self):
        (self.directory / "Ardor-CLAP-beta.zip").write_bytes(b"unexpected")
        with self.assertRaisesRegex(ValueError, "extra="):
            self.verify()

    def test_damaged_archive_prevents_publication(self):
        with (self.directory / self.names[0]).open("ab") as stream:
            stream.write(b"damaged")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.verify()

    def test_checksum_cannot_name_another_file(self):
        checksum = self.directory / (self.names[0] + ".sha256")
        checksum.write_text(checksum.read_text().replace(self.names[0], "../another.zip"))
        with self.assertRaisesRegex(ValueError, "exact archive"):
            self.verify()

    def test_bundle_version_must_match_release(self):
        self.archive(self.names[3], bundle_version="0.1.0")
        with self.assertRaisesRegex(ValueError, "bundle version"):
            self.verify()

    def test_missing_plugin_cannot_be_published_with_valid_checksum(self):
        for name in self.names:
            with self.subTest(name=name):
                self.archive(name, include_module=False)
                with self.assertRaises((ValueError, KeyError)):
                    self.verify()
                self.archive(name)

    def test_wrong_release_and_prerelease_versions_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing="):
            self.verify("0.1.1234")
        with self.assertRaisesRegex(ValueError, "major.minor.patch"):
            self.verify("0.1.9876-beta")


if __name__ == "__main__":
    unittest.main()
