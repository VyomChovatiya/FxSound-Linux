# Review of FxSound PR #678

Reviewed 2026-11-14 at head `5449b49dc8a8acf81d71127456ab5606b298b155`.
Source: https://github.com/fxsound2/fxsound-app/pull/678

The PR is open and targets main. It changes 236 files. The maintainer's November
9 comment proposes a new base after newer Windows/JUCE work is merged. Recheck
that branch decision before creating an overlapping upstream PR.

## Useful work

- **Original DSP reuse:** its CMake build compiles the upstream DfxDsp sources.
  Our custom engine is not that engine. Reusing this library behind the Qt UI is
  the strongest route toward measurable Windows sound parity.
- **Data-layout portability:** `pt_long` preserves Windows long semantics while
  using 32-bit storage on Linux. Relevant when adopting upstream DSP, not a
  reason to mechanically replace types in our custom implementation.
- **Audio thread boundaries:** PipeWire capture/playback communicate through
  rings with a DSP worker. Our current backend uses blocking libpulse streams
  on its worker thread; a native PipeWire migration should preserve that
  separation rather than run arbitrary DSP work in a real-time callback.
- **Volume:** the port separates DSP input level from hardware listening volume.
  Our sink is forced to unity, but that is not equivalent to bidirectional
  hardware-volume synchronization.
- **Routing:** review hardware classification and virtual-output loop prevention.
  Our enumeration excludes only FxSound itself and can still select another
  virtual processing sink. The PR discussion reports a fix for an EasyEffects
  feedback loop; verify the final implementation before transplanting it.
- **Desktop integration:** the proposed portal shortcuts address a real gap in
  our application-only QShortcut implementation. Its tray code needs adaptation
  to Qt, not direct copying of JUCE UI files.
- **CI:** Linux build/smoke testing and Windows shared-library MSVC builds are
  useful patterns. These checks do not establish complete Windows application
  compatibility or audio equivalence.

## Contribution recommendation

Publish this Qt experiment separately with accurate limitations. Do not open an
upstream PR that replaces the official UI and DSP wholesale. Start an upstream
branch from the maintainer's accepted base, reproduce one remaining issue, and
submit a small tested fix or feature. Candidate features from this project are
the optional bonus-preset setting and a current-preset reset control, but first
check whether the current upstream UI already provides equivalent behavior.

The local Qt rendering workaround does not apply directly to upstream's JUCE
interface. A native PipeWire/original-DSP migration is a separate implementation
task, not something established by this review.

## Publication preparation

Keep builds, logs, local settings, Windows binaries, and extracted fonts out of
Git. Verify resource licensing before public distribution. Keep assertions
enabled in the DSP test executable even for Release builds. A private source
repository can preserve the experiment while provenance and upstream scope are
resolved.
