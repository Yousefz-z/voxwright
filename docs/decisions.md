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

*2026-10-07.* The bundled soundboard pack is synthesized from scratch. No
third-party or meme clips are included, so there is nothing to license
and nothing binary to store in git. Superseded in part by
[D24](#d24-built-in-sounds-are-rendered-at-startup): the sounds are
rendered when the app starts, not at build time.

## D12. Text-to-speech

*2026-10-07.* Text-to-speech uses the platform voices through Qt
TextToSpeech. Routing speech into the virtual microphone needs PCM data,
which `QTextToSpeech::synthesize()` provides from Qt 6.6. The feature is
compiled only when Qt is 6.6 or newer; the pinned 6.8.3 satisfies that.

## D13. Neural voice conversion is optional

*2026-10-07.* The ML voice conversion track is built only with the CMake
option `VOX_ENABLE_ML=ON` (vcpkg feature `ml`). The DSP voices never
depend on it, and the default build and installers do not include it.
See [D34](#d34-onnx-runtime-comes-from-microsofts-release-archives) and
[ml.md](ml.md).

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

## D22. Settings are a JSON file

*2026-10-08.* Settings are saved as `settings.json` in the per-user
configuration folder instead of through `QSettings`. On Windows `QSettings`
writes to the registry, which users cannot back up, share, or repair by
hand; a JSON file is the same on both platforms, can be exported, and is
easy to test. Writes are atomic (`QSaveFile`). A file that does not parse
is renamed to `settings.json.damaged` and the user is told, rather than
the app crashing or silently starting over; a file from a newer version
loads the settings this version knows.

## D23. Device lists are polled every 2 seconds

*2026-10-08.* miniaudio reports events for open streams only (stopped,
rerouted), not devices being added or removed. The miniaudio backend
therefore compares the device lists every 2 s on a watcher thread and
reports a change; the engine and the app react to it (reopening a lost
device, picking a newly installed virtual cable). Native notifications
(`IMMNotificationClient` on Windows, a CoreAudio property listener on
macOS) would react faster but add two platform-specific code paths that
cannot be exercised here; 2 s is fast enough for plugging in a headset.

## D24. Built-in sounds are rendered at startup

*2026-10-08.* The 18 built-in sounds are code (`plugins/src/sound_pack.cpp`)
that the app renders on a worker thread while the soundboard loads,
instead of WAV files produced by a build-time tool. The installer
carries no audio files, a sound renders at the engine rate without
resampling, and a change to a sound is reviewed as a code diff. Every
sound is normalized to -18 LUFS (BS.1770) and limited to -1 dBFS; a test
checks loudness, peak, DC, quiet first and last samples, and that no two
sounds are near-duplicates. The soundboard file stores a built-in sound
as `builtin:<id>`, so a later version can improve a sound for everyone.

## D25. Global hotkeys never swallow keys

*2026-10-08.* A global hotkey also reaches the app that has focus: on
Windows the low-level keyboard hook always passes the key on, and on
macOS `RegisterEventHotKey` is used without an event tap. Swallowing keys
would need Accessibility permission on macOS and would break games that
use the same key. Push-to-talk and the censor key are handled on the
hook's thread (Windows) or the main run loop (macOS) by writing an atomic
the audio thread reads, so a busy UI cannot delay them. Linux builds have
no global hotkeys; the Hotkeys page says so instead of offering fields
that do nothing.

## D26. The bottom bar keeps its labels while they fit

*2026-10-08.* The on/off chips in the bottom bar show their names when
the whole bar fits and switch to icons with tooltips when it does not.
The width check measures the translated names, so a longer language
switches earlier instead of overflowing. The default window is 1280 px
wide so the labels show at the default size; the minimum is 960 px. A UI
test resizes the window from 1280 to 960 px with a sound playing and
checks that the input meter stays inside the window.

## D27. The user's voices are files; built-in voices are read-only

*2026-10-08.* Each voice the user saves is one JSON file in `voices/` in
the user data folder, in exactly the format of the built-in voices, and an
exported `.voxvoice` file is the same JSON. One file per voice means a
damaged file costs one voice, not all of them, and a voice can be shared by
copying a file. Built-in voices ship inside the program and are never
changed in place: "Customize a copy" saves a new voice, so an update can
improve a built-in voice without overwriting anyone's edits. Custom voice
ids are `custom-` plus random hex, which keeps them valid as file names.

## D28. Effects are reordered with buttons

*2026-10-08.* The designer moves an effect with "Move up" and "Move down"
buttons rather than by dragging. Buttons work with the keyboard and screen
readers, which drag and drop in Qt Quick does not, and they are exercised
by the UI test. Chains hold at most 12 effects, so a few clicks reach any
position. Dragging can be added later on top of the same `moveBlock()`.

## D29. A voice holds up to 12 effects and 4 quick sliders

*2026-10-08.* Each effect costs processing time on the audio thread, and
an unbounded chain (or an imported file with hundreds of blocks) could
not keep up in real time. The engine benchmark measures a voice of 12 of
the most expensive effects at 15 % of the block time on average and 34 %
at the 99th percentile on the development machine (architecture.md,
engine measurements). A machine three times slower would run out of time
on the slowest blocks with such a voice, so the designer and the Audio
page show the live processing load, and checklist item D4 measures the
slowest supported machine. Built-in voices use at most 5 effects and 3
quick sliders. The limits are checked when editing and when loading any file.

## D30. The tray uses Qt Widgets

*2026-10-08.* Qt Quick has no tray icon of its own. `QSystemTrayIcon` with a
`QMenu` gives the native tray on Windows and the menu bar extra on macOS,
so the app runs a `QApplication` instead of a `QGuiApplication` and links
Qt Widgets (LGPL-3, like the rest of Qt). Qt Labs Platform's tray would
avoid the dependency but is a technology preview. Where the desktop has no
tray, closing the window quits, and the settings that need a tray are
disabled with the reason.

## D31. Start at login on macOS uses a LaunchAgent

*2026-10-08.* "Start at sign-in" writes a LaunchAgent property list to
`~/Library/LaunchAgents` instead of calling `SMAppService.mainApp`.
`SMAppService` needs macOS 13 and an Objective-C++ code path that cannot be
compiled or tested here; a LaunchAgent works on every supported version
(macOS 12 and later), is plain XML that the tests read back, and macOS 13+
lists it under Login Items like any other. The same tested code writes the
Windows Run key and the Linux XDG autostart file.

## D32. Installers are built by CI and are not signed yet

*2026-10-08.* CI builds the Windows installer with Inno Setup and the macOS
disk image with `hdiutil`, from `cmake --install` trees in which Qt's
deployment tools have placed the Qt runtime. Signing needs certificates
(an Authenticode certificate for Windows, a Developer ID for macOS, plus
notarization) that belong to the project owner, so the installers are
unsigned and the README says how to open them. The installer installs per
user by default (no administrator rights) and removes the sign-in entry on
uninstall.

## D33. The virtual microphone check listens where chat apps listen

*2026-10-08.* The check plays a chirp (0.3 s, 400 Hz to 4 kHz, -12 dBFS)
through the engine's speech channel, which goes to the virtual microphone
exactly like a voice, and records the cable's recording side ("CABLE
Output" for VB-CABLE, the same device name for BlackHole) as a chat app
would. A normalized cross-correlation above 0.5 means the chirp arrived;
the test suite measures above 0.8 with the chirp 20 dB over noise and
below 0.3 for noise or speech alone. Silence and "something else arrived"
get their own messages, because they have different causes (the cable is
not installed or not selected, or another app plays into it).

## D34. ONNX Runtime comes from Microsoft's release archives

*2026-10-08.* The `ml` feature installs ONNX Runtime 1.23.2 through the
overlay port `onnxruntime-bin`, which downloads Microsoft's prebuilt CPU
release for the platform and checks its SHA-512, instead of vcpkg's
source port. Building ONNX Runtime from source pulls in abseil, protobuf,
and Eigen and takes over an hour per configuration, for the same library.
The release archives are what Microsoft documents for applications; the
pinned hashes keep the build reproducible.

## D35. Neural models are audio in, audio out

*2026-10-08.* Voxwright defines one model format: an ONNX graph from
`audio` (mono, at its stated rate) to `audio_out`, with an optional
`pitch_shift` input ([ml.md](ml.md#model-format)). Everything model
specific (content features, pitch estimation, the synthesizer) stays
inside the graph, so the app needs no per-architecture code and any
converter can be packaged for it. The streaming block cuts the voice into
overlapping windows on a worker thread and has a fixed latency, so the
audio thread never waits for inference and falls back to the plain voice
when a run is late.


## D36. Timing budgets in tests scale in sanitizer builds

*2026-10-08.* Correctness checks are identical in every build, but a
check against wall-clock time measures the instrumentation as much as the
code under a sanitizer. The voice chains render at most 0.065 of real time
in the optimized build, while ThreadSanitizer measured up to 0.33 on a
loaded machine, past the 0.25 budget. Sanitizer builds therefore define
`VOX_TESTING_INSTRUMENTED`, and timing checks multiply their budget by
`vox::testing::kTimingScale` (8 there, 1 elsewhere). The real-time budget
itself is enforced, unscaled, by the release and clang test runs.

## D37. macOS CI runs on demand

*2026-10-08.* The repository is private, where GitHub bills each macOS
runner minute as ten, and the two macOS test jobs took about 40 minutes
per run. No Mac is available to test on for now, so pull requests and
pushes to `main` get Linux and Windows CI, and the macOS jobs (tests,
disk image, and the driver compile check) run only when the workflow is
started by hand from the Actions tab.

## D38. Two MSVC warnings are off, each where it is noise

*2026-10-08.* The Windows build uses `/W4 /WX`, the counterpart of the
Linux warnings with `-Werror`. Two MSVC warnings fire on correct code and
have no counterpart in the GCC and Clang flags, so they are turned off:
C4324 (padding added by `alignas`) everywhere, because the lock-free
queues align their indices to cache lines on purpose, and C4702
(unreachable code) only for the C++ that qmlcachegen writes from the QML
files, where it fires inside inlined Qt headers. The project's own sources
keep C4702. On Windows the UI tests run the offscreen platform with the
system fonts (`QT_QPA_FONTDIR`), because the official Qt binaries ship
none. The test that speaks text into the virtual microphone needs the
flite engine and so still runs on Linux only; text to speech on Windows
is covered by row T9 of the [manual checklist](manual-test-checklist.md).

## D39. The Visual C++ runtime is installed next to the program

*2026-10-08.* Voxwright and Qt link the Visual C++ runtime as DLLs. The
Windows installer runs without administrator rights (D32), so it cannot
run Microsoft's runtime setup program, which windeployqt would otherwise
add. `cmake --install` therefore copies the runtime DLLs into the
program's `bin` folder (CMake's InstallRequiredSystemLibraries), where
Windows loads them before any system wide copy. The cost is a few
megabytes per install and no automatic runtime updates through Windows
Update; a new runtime arrives with the next Voxwright release.
