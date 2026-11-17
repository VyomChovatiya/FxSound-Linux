# Original DSP integration experiment

The original DSP integration is now the main implementation on `main`.

Uses the original FxSound DSP and Linux compatibility layer from
https://github.com/biduzimr0x/fxsound-app at
`3d58d985524dad6889179d00f6220ce308d7b3a9`.
Original source notices are preserved under vendor/fxsound.

The existing Qt UI, preset manager, bonus pack, translations, reset control,
visualizer, and PulseAudio backend remain in place. Effect and EQ parameters
are forwarded to the original DSP. The custom effect processing chain is no
longer used. Optional loudness normalization, listening modes and the output
ceiling remain application extensions, so this is not a bit-identical Windows
output claim. The old analytical normalization model still needs recalibration
against the original DSP. Audio processing currently allocates a dry buffer and
shares the parameter mutex; this is an experimental integration, not an RT audit.
Resetting custom analysis states does not yet explicitly flush the original
engine's reverb state. Live output, Bluetooth, reconnect and listening comparisons
remain to be tested. The separate PipeWire backend is not included.

Build directly in the project directory:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/fxsound
```

Artwork and presets are bundled beside the executable, so it can launch from any directory. The current executable is `build/fxsound`. No temporary build directory is required.
