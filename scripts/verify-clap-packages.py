#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the exact CLAP release set; shared by PR CI and release publication."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import re
import tarfile
import zipfile


def package_names(version):
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version):
        raise ValueError("CLAP package version must be numeric major.minor.patch")
    return [
        f"Ardor-CLAP-{version}-macOS-arm64.zip",
        f"Ardor-CLAP-{version}-Windows-x64.zip",
        f"Ardor-CLAP-{version}-Linux-x64.tar.gz",
        f"ArdorNAM-{version}-macOS-arm64.zip",
        f"ArdorNAM-{version}-Linux-x64.tar.gz",
    ]


def verify(directory, version):
    directory = Path(directory)
    archives = package_names(version)
    expected = set(archives) | {name + ".sha256" for name in archives}
    actual = {path.name for path in directory.iterdir()}
    if actual != expected:
        raise ValueError(f"Incomplete or unexpected CLAP release set: missing={sorted(expected-actual)}, extra={sorted(actual-expected)}")
    for name in archives:
        path = directory / name
        checksum = directory / (name + ".sha256")
        if path.is_symlink() or checksum.is_symlink() or not path.is_file() or not checksum.is_file():
            raise ValueError(f"Package and checksum must be ordinary files: {name}")
        record = re.fullmatch(r"([a-fA-F0-9]{64}) [ *]([^\r\n]+)\s*", checksum.read_text(encoding="utf-8").strip())
        if not record or record[2] != name:
            raise ValueError(f"Checksum must name its exact archive: {checksum.name}")
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != record[1].lower():
            raise ValueError(f"Checksum mismatch: {name}")

        nam = name.startswith("ArdorNAM-")
        root, module = ("ArdorNAM", "ArdorNAM") if nam else ("ardor-clap", "Ardor")
        notice = "licenses/CLAP.txt" if nam else "licenses/ardor-clap/CLAP.txt"
        if name.endswith(".zip"):
            with zipfile.ZipFile(path) as archive:
                if archive.testzip() is not None:
                    raise ValueError(f"Corrupt archive: {name}")
                read = archive.read
                if "macOS" in name:
                    info = plistlib.loads(read(f"{root}/{module}.clap/Contents/Info.plist"))
                    if (info.get("CFBundleShortVersionString"), info.get("CFBundleVersion"), info.get("CFBundleExecutable")) != (version, version, module):
                        raise ValueError(f"Mac bundle version/executable does not match release: {name}")
                    binary = read(f"{root}/{module}.clap/Contents/MacOS/{module}")
                else:
                    binary = read(f"{root}/{module}.clap")
                if not binary or not read(f"{root}/README.md") or not read(f"{root}/{notice}"):
                    raise ValueError(f"Package is missing module, guide or CLAP notice: {name}")
        else:
            with tarfile.open(path, "r:gz") as archive:
                for member in (f"{root}/{module}.clap", f"{root}/README.md", f"{root}/{notice}"):
                    entry = archive.getmember(member)
                    if not entry.isfile() or not entry.size:
                        raise ValueError(f"Package is missing a regular nonempty file: {member}")
        print(f"Verified {name}")
    return [directory / name for name in sorted(expected)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        verify(args.directory, args.version)
    except (ValueError, OSError, KeyError, EOFError, zipfile.BadZipFile, tarfile.TarError, plistlib.InvalidFileException) as error:
        parser.exit(1, f"CLAP release validation failed: {error}\n")


if __name__ == "__main__":
    main()
