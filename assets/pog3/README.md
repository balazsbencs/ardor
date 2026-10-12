# Pedal FFTW planning seed

`fftw-neon.wisdom` is the FFTW 3.3.10 single-precision NEON planning seed used
by the Pi 4 timing comparisons. Buildroot installs it as
`/usr/share/ardor-pedal/pog3-fftw.wisdom`. POG3 imports it once under the FFTW
planner mutex during preparation on Linux ARM64. Processing never reads a file
or plans a transform. Other architectures plan normally.

If the seed is missing or incompatible, FFTW can still prepare plans using
MEASURE. Retest device throughput when changing FFTW, compiler/CPU targets or
the seed; a mathematically valid plan is not a CPU admission result.
