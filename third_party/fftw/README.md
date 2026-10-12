# FFTW dependency notice

POG3 links the single-precision FFTW library (GPL-2.0-or-later).
`COPYING` is copied from the official FFTW 3.3.10 source release.
The pinned source is https://www.fftw.org/fftw-3.3.10.tar.gz, SHA-256
`56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467`.

Pedal builds use Buildroot's static NEON library and packaged license.
Desktop/CLAP CI uses `scripts/build-fftw-single.py` to build a static,
position-independent float library with threads/OpenMP disabled, preserving
self-contained app/plugin packages. The script exposes its install prefix to
subsequent CMake steps. Desktop and CLAP packages include this license notice.
The project also accepts explicitly configured system/SDK FFTW paths.
