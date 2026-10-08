# Decisions

Scope and design decisions, recorded so they are not quietly undone.
Change an entry only with a new dated entry that supersedes it.

## D1. Clean-room scope

*2026-10-07.* Features are reproduced from public behavior only: help
center articles, marketing pages, third-party integration docs, and
reviews. No binaries, installers, drivers, presets, sound packs, icons,
or branding of the reference product are downloaded or inspected. The
reference product is not named anywhere in this repository. Voice names,
effect chains, sounds, icons, and UI are original.

## D2. Product name

*2026-10-07.* The product is called **Voxwright**. A web search found no
product with this name; the closest match is an unrelated dictation
browser extension with a different spelling.

## D3. Platforms

*2026-10-07.* Shipping targets are Windows 11 (x64) and macOS 12 or later
(universal binary). Linux builds and runs in CI and in the development
container for testing, but is not a shipping target: global hotkeys and
autostart are implemented for it only where trivial, and it has no
installer.

## D4. Dependency pinning

*2026-10-07.* All C and C++ libraries come from vcpkg manifest mode
(`vcpkg.json`, `vcpkg-configuration.json`) with **overlay ports in
`ports/` that fetch sources by full git commit hash**. Reasons:

* Stock vcpkg ports download GitHub archive tarballs. Those downloads are
  blocked in the development container while git fetches are allowed,
  so stock ports cannot be verified there. Overlay ports using
  `vcpkg_from_git` behave identically in the container and in CI.
* A commit hash pins content more strictly than a version tag.
* Conan was rejected: Conan Center is unreachable from the development
  container, so its lockfile could not be exercised before committing.

Qt is the exception. Building Qt through vcpkg takes over an hour per CI
job. CI installs the official Qt 6.8.3 binaries with `aqtinstall` (exact
version pinned in the workflow); the development container builds the
same 6.8.3 tag from source. Updating any dependency means changing the
commit in its overlay port and the version table in
[architecture.md](architecture.md#dependencies).

## D5. RNNoise version

*2026-10-07.* RNNoise is pinned to v0.1.1. Version 0.2 has a better model
but its build downloads the weights from a separate host instead of
carrying them in the tagged sources, which adds an unpinned artifact to
the supply chain and is unreachable from the development container.
Upgrade path: add the v0.2 weight archive to the overlay port with a
SHA-512 check once that host is reachable from the build environment.

## D6. Audio format

*2026-10-07.* The engine processes at 48 kHz, 32-bit float. The voice path
is mono: microphones are mono sources and chat applications downmix to
mono. Effects that would benefit from stereo (reverb, chorus) produce a
mono result. Output devices receive the mono signal on every channel.

## D7. UI toolkit

*2026-10-07.* Qt 6 Quick, not JUCE. Qt is LGPL-3 when dynamically linked;
JUCE 8 is AGPL-3 or commercial. Qt offers the native features needed
(tray, text-to-speech, dialogs, high-DPI). Details in
[architecture.md](architecture.md#user-interface).

## D8. Audio I/O library

*2026-10-07.* miniaudio, not PortAudio. Single-file integration, WASAPI
exclusive and shared, CoreAudio, device notifications, and built-in file
decoders. Rate conversion is not delegated to it; the engine uses
libsamplerate.

## D9. Virtual microphone

*2026-10-07.* The shipped baseline relies on third-party loopback drivers
that the user installs from their official sites: VB-CABLE on Windows,
BlackHole 2ch on macOS. The installer does not bundle them (VB-CABLE's
license does not allow redistribution without an agreement; BlackHole is
GPL-3 and better installed through its own package). The first-run flow
detects them, links to the download pages, and verifies them with a
loopback test. A first-party driver is designed (`drivers/`) but needs
platform toolchains, code signing, and hardware testing that this
environment does not have.

## D10. No network access, no accounts

*2026-10-07.* The app makes no network requests: no accounts, telemetry,
update checks, or online catalogs. Sharing works through exported files.

## D11. Bundled sounds

*2026-10-07.* The bundled soundboard pack is synthesized from scratch by
an internal tool at build time. No third-party or meme clips are
included, so there is nothing to license and nothing binary to store in
git.

## D12. Text-to-speech

*2026-10-07.* Text-to-speech uses the platform voices through Qt
TextToSpeech. Routing speech into the virtual microphone needs PCM data,
which `QTextToSpeech::synthesize()` provides from Qt 6.6. The feature is
compiled only when Qt is 6.6 or newer; the pinned 6.8.3 satisfies that.

## D13. Neural voice conversion is optional

*2026-10-07.* The ML voice conversion track is built only with the CMake
option `VOX_ENABLE_ML=ON` (vcpkg feature `ml`). The DSP voices never
depend on it, and the default build and installers do not include it.

## D14. Project license

*2026-10-07.* No license file is added; choosing one is the owner's
decision. Third-party licenses are listed in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and shipped with the
installers.

## D15. Naming and style

*2026-10-07.* Types `PascalCase`, functions and variables `camelCase`,
private members with a trailing underscore, constants `kPascalCase`,
namespaces lowercase, files `snake_case`. Enforced by clang-format and
clang-tidy (`readability-identifier-naming`).

## D16. Voices are tuned by measurement on synthetic speech

*2026-10-08.* The development environment has no speakers or microphone,
so voices are tuned and verified by measurement (pitch, loudness, latency,
spectral features, spectrograms) on a synthetic speech phrase with known
pitch and formants, not by listening. No third-party speech recordings are
committed. Listening checks on real voices are part of
[manual-test-checklist.md](manual-test-checklist.md).

## D17. Cut filters are 24 dB per octave

*2026-10-08.* The equalizer's low and high cuts are fourth-order
Butterworth. Twelve dB per octave left audible energy outside the band of
the device voices after clipping and rate reduction (found on the
spectrogram sheet).

## D18. The microphone's clock drives processing

*2026-10-08.* While a microphone is open, its capture callback runs the
whole graph and every output reads from a drift-compensated ring buffer
("push mode"). The alternative, driving from the virtual microphone's
callback and buffering the input, adds the same buffering on the input
side and gives the microphone no say in timing; push mode keeps the
voice path to one ring buffer per output. Without a microphone the
virtual-mic output's callback drives instead ("pull mode"), so the
soundboard and speech never depend on a microphone being present.

## D19. Drift control measures a continuous fill, in two speeds

*2026-10-08.* Each output's ring buffer is held at a 2 ms target by a PI
controller on the fill the buffer would have if the device read
continuously, measured once per producer callback. Holding the raw fill
instead made the controller chase the beat between the two callback
rates (+-1800 ppm ratio swings, 5 dB tone-to-noise in simulation). One
fixed loop speed could not both acquire a 300 ppm clock difference within
the 2 ms margin and keep scheduling jitter out of the ratio, so the loop
acquires fast for 20 s after a start or refill and then tracks slowly.
Measured: no dropouts at +-300 ppm with 1.5 ms jitter and 97 dB
tone-to-noise ([architecture.md](architecture.md#engine-measurements)).

## D20. Noise suppression costs no latency while off

*2026-10-08.* RNNoise adds 20 ms (two 10 ms frames: one to collect, one
inside its overlap-add). It runs all the time so its
state is current, but while off the graph uses the undelayed input and
crossfades to the suppressed signal only when it is switched on. The
latency readout includes the 20 ms only while it is on. Until milestone
3 the wrapper reported 10 ms and delayed its dry signal by only one
frame, so partial strengths blended two copies 10 ms apart; the engine
latency measurement exposed it, and a test now pins both paths to the
same 20 ms.

## D21. Voice switches crossfade linearly

*2026-10-08.* Old and new voices are both derived from the same
microphone and are strongly correlated, so the 20 ms switch uses a
constant-gain (linear) crossfade. An equal-power crossfade measured a
+3 dB swell halfway through (0.57 peak for a 0.4 input).
