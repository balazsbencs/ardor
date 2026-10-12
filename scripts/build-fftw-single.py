#!/usr/bin/env python3
"""Build the pinned static FFTW float library used by desktop/plugin CI."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import urllib.request

VERSION = "3.3.10"
SHA256 = "56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    workspace = prefix.parent / "ardor-fftw-single-build"
    workspace.mkdir(parents=True, exist_ok=True)
    archive = workspace / f"fftw-{VERSION}.tar.gz"
    if not archive.exists():
        with urllib.request.urlopen(f"https://www.fftw.org/fftw-{VERSION}.tar.gz", timeout=60) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError("FFTW source archive does not match the pinned version")
    source = workspace / f"fftw-{VERSION}"
    if not source.exists():
        subprocess.run(["cmake", "-E", "tar", "xzf", str(archive)], cwd=workspace, check=True)
    build = workspace / "build"
    command = ["cmake", "-S", str(source), "-B", str(build),
               "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DCMAKE_BUILD_TYPE=Release",
               "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_SHARED_LIBS=OFF",
               "-DBUILD_TESTS=OFF", "-DENABLE_FLOAT=ON", "-DENABLE_THREADS=OFF",
               "-DENABLE_OPENMP=OFF", "-DDISABLE_FORTRAN=ON",
               f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}"]
    if platform.machine().lower() in ("x86_64", "amd64"):
        command += ["-DENABLE_SSE=ON", "-DENABLE_SSE2=ON"]
    if platform.system() == "Windows":
        command += ["-A", "x64"]
    subprocess.run(command, check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", "3"], check=True)
    subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)
    if "GITHUB_ENV" in os.environ:
        previous = os.environ.get("CMAKE_PREFIX_PATH", "")
        value = prefix.as_posix() + (";" + previous if previous else "")
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as environment:
            environment.write(f"CMAKE_PREFIX_PATH={value}\n")


if __name__ == "__main__":
    main()
