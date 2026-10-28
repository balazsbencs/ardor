# Ardor NAM CLAP editor

Mode: Operate. Platform: native macOS Cocoa, embedded in a CLAP host.

A guitarist at a desk loads a model, adjusts its actual learned controls and
compares the sizes the file contains. The user's brief is a simple, clean UI
without visual theatrics. Extend Ardor's dark neutral/bone language with familiar
native controls and the platform system font. Avoid the full pedal-chain editor.
The explicit request adds this desktop plugin surface to the existing product.

One model header and Open Model action lead into a scrollable list of labeled
physical controls. Sliders have editable numeric values; discrete controls use
their exported option names. Model size is a native menu, disabled for single
models. Input/output controls sit below the model, with calibration disclosed
only on demand. Loading, missing metadata, rejected files and host restarts remain
visible in one status line. Keyboard focus and Retina rendering use Cocoa.

The DSP runtime, its supported formats and parameter definitions own the product
facts. No implicit second model, invented physical units, decorative knobs or
unverified performance promises are introduced.
