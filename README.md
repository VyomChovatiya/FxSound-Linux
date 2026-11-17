# ReverseFxSound

An experimental native Linux audio enhancer with a Qt 6 interface inspired by
FxSound. This is an independent project, not an official FxSound release.

The application uses the original FxSound DSP with a Linux compatibility layer,
connected to a custom Qt interface and a PulseAudio backend. It runs with
PipeWire's PulseAudio compatibility service or PulseAudio; live testing so far
has focused on CachyOS with PipeWire. Windows builds are not supported.

## Features

- Five effect controls and nine visible EQ controls with adjustable frequencies.
- Twelve factory presets and twelve optional bonus music presets.
- Custom preset save, rename, delete, import and export in `.fac` format.
- A direct reset button that restores the current preset's saved/default values.
- An audio-driven spectrum display with outward-moving bars and an eye toggle.
- Language selection, tray controls, and application-scoped keyboard shortcuts.
- Optional loudness normalization and listening-level modes.

The virtual sink captures application audio and sends the processed result to
the physical output selected in the app. Capture and playback streams are pinned
to their intended endpoints to prevent automatic moves from creating feedback.
The virtual volume control starts at the hardware volume and forwards subsequent
changes to the hardware; capture compensates for virtual attenuation.

## Build and run

Requirements: C and C++ compilers with C++20 support, CMake 3.20+, Python 3,
pkg-config, Qt 6 Core/Gui/Widgets/Svg, libpulse and libpulse-simple development
libraries, and `pactl` at runtime. An active PulseAudio-compatible audio server is
required for playback.

From the project directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/fxsound
```

The executable is `build/fxsound`. Artwork, translations, fonts available locally,
and preset data are copied into `build/resources`. Keep that folder beside the
executable if copying the build elsewhere. Launching does not depend on the
working directory, and no temporary build directory is required.

The app changes the system default output to `FxSound_Audio_Enhancer`. Select the
intended physical output inside FxSound. Volume changes can also change the
hardware output volume. Use the tray's **Exit** action to quit completely before
relaunching an updated build; closing the window may leave the app running.

## Presets

Enable the bonus music pack in Settings. The circular-arrow button above the EQ
restores the current factory or bonus preset; a custom preset returns to its
saved state. Import and export use `.fac` files. User settings and custom presets
are stored separately from the bundled resources using Qt's configuration paths.

## Validation and limitations

The build includes tests for presets and DSP behavior, plus the original engine's
smoke test. Assertions remain enabled for the custom tests in Release builds.
These checks do not establish identical Windows output or full feature parity.
Optional normalization, listening modes and the output ceiling change the final
signal beyond the original DSP.

Routing and volume fixes have been exercised locally, but Bluetooth, hotplug,
server restarts, volume changes made directly on hardware, and other desktops
need broader testing. The backend is not a native PipeWire implementation.
Keyboard shortcuts are application-scoped, not desktop-global. The Wayland
full-window repaint workaround still needs testing across compositors.
See [integration notes](docs/original-dsp-integration.md) for remaining DSP and
real-time processing limitations.

To inspect the UI without starting audio processing:

```sh
./build/fxsound --no-virtual-sink
```

To run offscreen UI checks with isolated settings and capture screenshots:

```sh
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$PWD/build/ui-test-config" \
  ./build/fxsound --dump-screenshots "$PWD/build/ui-captures"
```

Offscreen checks cover resource loading and UI interactions; they cannot prove
that live compositor rendering or audio device switching is correct.

## Upstream work and attribution

The original DSP is from [FxSound](https://github.com/fxsound2/fxsound-app).
The Linux compatibility layer and DSP smoke test were adapted from
[biduzimr0x's Linux port](https://github.com/biduzimr0x/fxsound-app/tree/feature/linux-pipewire-port),
pinned at `3d58d985524dad6889179d00f6220ce308d7b3a9`.
See [vendor provenance](vendor/fxsound/ORIGIN.md).

[PR #678](https://github.com/fxsound2/fxsound-app/pull/678) is a separate Linux
porting effort. The [earlier review](docs/pr-678-review.md) records the comparison
made before this project adopted the original DSP.

## License

Project code is distributed under AGPL-3.0; see [LICENSE](LICENSE). Original
copyright notices are preserved. FxSound names, artwork, translations and preset
data retain their original ownership. No trademark rights or official endorsement
are implied. See [THIRD_PARTY.md](THIRD_PARTY.md) for resource provenance and
redistribution questions. Build output, Windows reference binaries and local
analysis artifacts are excluded from Git.
