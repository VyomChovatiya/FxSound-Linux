# ReverseFxSound

An experimental Linux audio enhancer with a Qt 6 interface inspired by FxSound.
This is an independent project, not an official FxSound release.

The application provides five effect controls, a nine-control EQ, a spectrum
display, factory and optional bonus presets, and a virtual playback sink.
The audio backend uses libpulse and works through PipeWire's PulseAudio
compatibility service or PulseAudio.

## Status

On the `original-dsp-integration` branch the original FxSound DSP replaces the custom effects. See [integration notes](docs/original-dsp-integration.md). The main branch uses a custom implementation. Identical sound or feature parity with the
Windows FxSound engine has **not** been established. Existing DSP tests check
this implementation's behavior; they are not Windows reference comparisons.

Keyboard shortcuts are application-scoped, not desktop-global. Bluetooth and
device reconnection need broader testing. A full-window repaint workaround is
enabled for reported Wayland corruption; it still needs confirmation on affected
desktops. Closing the main window may leave the application running in the tray.

## Build

Requirements: a C++20 compiler, CMake 3.20+, pkg-config, Qt 6 Core/Gui/Widgets/Svg,
libpulse development libraries, and `pactl` at runtime.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/fxsound
```

Run from the repository checkout so assets and presets can be located. The app
changes the default audio sink while processing is active. Select the intended
physical output in the output selector. Use the tray's Exit action to quit.

`--no-virtual-sink` opens the application without starting the audio backend.
`--dump-screenshots <directory>` performs offscreen UI checks and writes captures;
use an isolated configuration directory for these checks:

```sh
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME=/tmp/reversefxsound-ui-test \
  ./build/fxsound --dump-screenshots /tmp/reversefxsound-ui-captures
```

## Presets and interface

Enable the 12 additional music presets in Settings. The circular-arrow button
above the EQ restores the current factory/bonus preset; custom presets return
to their saved state. Preset files use the `.fac` format.

## Related work

[FxSound](https://github.com/fxsound2/fxsound-app) publishes its original DSP and
JUCE application. [PR #678](https://github.com/fxsound2/fxsound-app/pull/678)
ports that code to Linux. See [the comparison](docs/pr-678-review.md) for the
distinction and recommended next steps.

## License and attribution

Project code is distributed under AGPL-3.0; see LICENSE. FxSound names, artwork,
translations, and preset data retain their original ownership. No trademark
rights or official endorsement are implied. See THIRD_PARTY.md for resource
provenance and exclusions.
