# Architecture

Voxwright is a real-time voice changer and soundboard for Windows 11 and
macOS. This document explains how it is built and why, with the
measurements behind each major choice. Numbers in this document come from
runs in the development container (4 vCPU x86-64, GCC 13.3, `-O2`, no
audio hardware) unless stated otherwise; the commands that produce them
are listed in [Reproducing the numbers](#reproducing-the-numbers).

## Layers

```
            +--------------------------------------------------------+
  app/      |  Qt Quick UI, view models, tray, hotkeys, autostart,   |
            |  settings, first-run flow, text-to-speech capture      |
            +-------------------+------------------+-----------------+
                                | commands (lock-free queues)        |
            +-------------------v------------------+                 |
  engine/   |  AudioEngine: graph, transmit control,|<---------------+
            |  soundboard player, mixer, limiter,   |  device events
            |  drift compensation, meters           |
            +----------+---------------+------------+
                       |               |
            +----------v-------+  +----v------------------------+
  plugins/  | effect registry, |  | devices/: AudioBackend       |
            | voice presets    |  | (miniaudio: WASAPI,         |
            | (JSON), macros   |  |  CoreAudio, ALSA/Pulse),    |
            +----------+-------+  |  FakeBackend for tests,     |
                       |          |  decoder, cable detection    |
            +----------v-------+  +-----------------------------+
  dsp/      | effects, filters,|
            | pitch, analysis  |
            +----------+-------+
  core/     | Result/Error, lock-free queues, ring buffers
            +------------------
```

Dependencies only point downward. `dsp/` and `core/` know nothing about
devices, threads, files, or Qt; `engine/` knows nothing about Qt;
`app/` is the only Qt module. Every layer below `app/` is tested directly
through Catch2 without a UI or audio device.

| Module | Contents | Third-party code |
|---|---|---|
| `core/` | `Result<T>`, typed `Error` with actionable messages, SPSC queue, SPSC ring buffer, triple buffer | none |
| `dsp/` | Biquad/SVF filters, delay lines, LFOs, envelope followers, pitch tracker, PSOLA pitch shifter, spectral shifter, vocoder, FDN reverb, dynamics, distortion, modulation effects, ambience synthesis, loudness meter, RNNoise wrapper, offline render helper used by tests | Signalsmith Stretch (MIT), RNNoise (BSD-3) |
| `plugins/` | Effect descriptors (parameter metadata), factory registry, node wrappers, preset and macro model, JSON preset parser, 54 voice presets | nlohmann/json (MIT) |
| `engine/` | Audio graph, voice chain hot swap, transmit control (mute, push-to-talk, push-to-mute, censor), soundboard player, mixer buses, output limiter, drift compensation, metering, command and event queues | libsamplerate (BSD-2) |
| `devices/` | `AudioBackend` interface, miniaudio backend, fake backend, audio file decoding, virtual cable detection, loopback test | miniaudio (MIT-0) |
| `app/` | QML UI, view models, global hotkeys, tray, autostart, settings store, text-to-speech capture, first-run flow | Qt 6.8 (LGPL-3) |
| `drivers/` | macOS AudioServerPlugIn loopback driver source; Windows driver design | none |
| `tools/` | Internal development tools only: benchmarks, sound synthesis for the bundled sound pack, analysis scripts, ML export and benchmark. Not shipped. | none |

## Signal flow

All processing runs at 48 kHz, 32-bit float, mono for the voice path.
Output devices receive the mono mix on every channel.

```
mic device --(rate convert)--> input gain --> noise suppression --> gate
     --> transmit control (mute / PTT / PTM / censor)
     --> voice chain (active preset, crossfaded on switch, bypassable)
     --> + ambience (global background toggle)
     |
     +--> voice bus ----------------+-----------------------------+
                                    |                             |
soundboard player --> sound bus ----+                             |
tts player --------> tts bus -------+                             |
                                    v                             v
                         virtual mic mix               monitor mix (hear myself)
                         --> limiter                   (voice only if enabled,
                         --> (rate convert)             sounds unless "mute for me")
                         --> virtual cable device       --> limiter --> headphones
```

## Threads

| Thread | Work | Rules |
|---|---|---|
| Capture callback (device) | Pull mic frames, run the whole graph, push results into one ring buffer per output device | No locks, no allocation, no system calls other than the device API returning. Verified by an allocation trap in tests. |
| Playback callbacks (one per output device) | Pop from the ring buffer through the drift compensator | Same rules. |
| Control thread (Qt main thread) | UI, building voice chains, decoding sounds, settings | Never touches audio state directly; sends commands. |
| Hotkey thread (Windows) | Low-level keyboard hook message loop | Posts events to the control thread and writes push-to-talk state straight into an atomic so it does not wait for the UI. |

Communication between control and audio threads uses three primitives
from `core/`:

* **Command queue** (SPSC, fixed capacity): parameter changes, toggles,
  sound triggers. Drained at the start of every audio block.
* **Mailbox** for large objects (a newly built voice chain, a decoded
  clip): ownership passes by pointer through the queue; the audio thread
  hands retired objects back through a second queue so they are destroyed
  on the control thread, never on the audio thread.
* **Event queue** (SPSC, audio to control): meters, xrun counts,
  feedback detection, clip finished.

## Pitch and formant shifting

Pitch shifting quality decides how good most voices sound, so two engines
were built and measured against each other on the same material.

1. **PSOLA shifter** (`dsp::PsolaShifter`, written for this project). A
   pitch tracker (YIN cumulative-mean-normalized difference on a 4x
   decimated signal every 2.7 ms, refined on the full-rate signal with
   parabolic interpolation, median of three against octave errors) finds
   the period. Pitch marks are placed one period apart and aligned by
   normalized cross-correlation with the previous mark, so every grain
   starts at the same phase of the cycle. Two-period Hann grains are
   overlap-added at the target period. Formants move independently by
   resampling each grain before it is added. Unvoiced frames use a fixed
   5 ms grain spacing so noise passes through unchanged. Grains are
   rendered lazily, just ahead of emission, so each one uses the pitch mark
   nearest its own time and the measured delay equals the reported latency
   (test `PSOLA latency matches the reported value`).
   Latency is 2.25 times the longest expected period plus 8 samples:
   1448 samples (30.2 ms) for a 75 Hz minimum pitch.
2. **Spectral shifter** (`dsp::SpectralShifter`, wraps Signalsmith
   Stretch 1.4). Phase-vocoder transposition with optional cepstral formant
   compensation.

Result ([Pitch engine comparison](#pitch-engine-comparison)): PSOLA is
more accurate, much cleaner, and has lower latency for live speech. The
spectral engine needs analysis blocks several pitch periods long (its
library default is 120 ms); at 20 to 43 ms it makes octave errors on low
voices, and its formant compensation does not hold close vowel formants in
place even when given the true fundamental (an F2 of 1077 Hz moved to
2473 Hz for a 180 Hz voice shifted down 5 semitones). Every live voice
therefore uses PSOLA. The spectral engine is kept for offline
transposition of soundboard clips, where latency is irrelevant and clips
are often polyphonic.

Rejected alternatives:

* **Rubber Band**: excellent quality, but GPL or a paid license, and its
  R3 engine has more latency than the targets here.
* **Plain delay-line (two-tap) shifter**: lowest latency, but it moves
  formants with pitch and adds flanging. PSOLA with the formant ratio set
  equal to the pitch ratio produces the same "chipmunk" sound when a voice
  wants it, so no separate engine is needed.

## Noise suppression

RNNoise v0.1.1: 48 kHz native, 10 ms frames (480 samples of latency), a
recurrent model small enough for any CPU. Suppression of stationary noise
after one second of adaptation (`vox_bench_noise`, see
[Reproducing the numbers](#reproducing-the-numbers)):

| Noise | Input level (dBFS) | Reduction after 1 s (dB) |
|---|---|---|
| White | -58.7 | -30.3 |
| White | -38.7 | -1.6 |
| White | -24.8 | -6.0 |
| Pink | -53.5 | -34.2 |
| Pink | -33.5 | -38.3 |
| Pink | -19.5 | -16.8 |
| Brown | -57.5 | -43.7 |
| Brown | -37.5 | -39.4 |
| Brown | -23.5 | -48.4 |

The model removes 17 to 48 dB of room-like (pink and brown) noise at every
level tested and 30 dB of quiet white noise, but leaves loud full-band white
noise mostly in place (2 to 6 dB). Real microphone noise is pink or brown (fans, traffic, hum), and the
noise gate covers the remaining hiss between words. The unit test
`Noise suppressor removes stationary noise and keeps speech` checks a
phrase in pink noise at about 10 dB SNR: 16.9 dB less noise in pauses, and
the vowel level changes by less than 0.1 dB.

DeepFilterNet gives better suppression of non-stationary noise but needs a
20 ms frame plus look-ahead and several times the CPU; it is listed for
evaluation in the ML track. RNNoise v0.2 has a better model but downloads
its weights from a separate host at build time; see
[decisions.md](decisions.md#d5-rnnoise-version).

## Audio I/O

miniaudio 0.11.25 over PortAudio:

* One C file, no build system to integrate, MIT-0 license.
* WASAPI shared and exclusive mode, CoreAudio, ALSA/PulseAudio, and a
  null backend, with device enumeration and change notifications.
* Bundles WAV, MP3, and FLAC decoders (plus stb_vorbis for OGG), which
  the soundboard needs anyway.
* PortAudio would add a second build, and its WASAPI exclusive support
  and device notifications are weaker.

The devices layer opens every device at its native rate and channel
count. Rate conversion and drift compensation happen in the engine with
libsamplerate so quality does not depend on the backend's built-in
linear resampler.

### Latency budget

Round trip = capture period + ring buffer target + playback period +
device and driver buffering + algorithmic latency of the active voice.
With a 128-frame period at 48 kHz (2.67 ms) the engine contributes
5.3 ms plus 1 period of safety in the ring buffer: 8 ms. Windows shared
mode adds the audio engine's period (typically 10 ms, lower with
`IAudioClient3` on drivers that support it); exclusive mode removes it.
The measured engine-side figures are in
[Engine measurements](#engine-measurements); the device-side part must
be measured on real hardware ([manual-test-checklist.md](manual-test-checklist.md)).

## Virtual microphone

The engine treats the virtual microphone as a normal output device. Any
loopback driver (VB-CABLE, BlackHole, or a first-party driver) exposes a
render endpoint the engine plays into and a capture endpoint that chat
apps record from. This keeps the engine free of driver-specific code:
the first-run flow detects the cable by name and verifies it with a
loopback chirp test.

The first-party driver path is designed but not shippable from this
environment:

* `drivers/macos/`: AudioServerPlugIn loopback device in C (one output
  stream, one input stream, a shared ring buffer inside the plug-in).
  Building requires Xcode; installing requires a Developer ID signature
  and notarization.
* `drivers/windows/README.md`: design for a WaveRT virtual audio device
  pair (render and capture) based on the Windows driver samples,
  requiring the WDK, an EV certificate, and Microsoft attestation
  signing.

## User interface

Qt 6.8 LTS with Qt Quick (QML), chosen over JUCE:

| | Qt 6 Quick | JUCE 8 |
|---|---|---|
| UI model | Declarative QML, GPU scene graph, built-in animations, states, transitions | Imperative `Component` painting on CPU (or OpenGL context) |
| Native integration | System tray, file dialogs, text-to-speech, high-DPI, accessibility, autostart via platform APIs | Tray and dialogs available; no text-to-speech |
| License | LGPL-3 for every module used (dynamic linking satisfies it) | AGPL-3 or a paid license for closed distribution |
| Audio | Not used (miniaudio handles audio) | Its main strength, not needed here |

Global hotkeys are not part of Qt, so `app/platform/` implements them per
OS (see [feature-matrix.md](feature-matrix.md#12-global-hotkeys)).

## Effects as data

Every effect block registers an `EffectDescriptor` with parameter
metadata (id, name, unit, range, skew, default). Voices are JSON files
that name blocks and parameter values and declare macro sliders mapped to
block parameters. The designer UI renders any block from its descriptor,
so adding an effect means writing the DSP class and one registration;
adding a voice means writing JSON.

## Dependencies

All C and C++ dependencies come from vcpkg in manifest mode. Each one is
an overlay port in `ports/` that fetches its source by full commit hash
over git, so builds are reproducible without trusting tags or archive
hosting. `vcpkg.json` pins the vcpkg registry baseline used for the
helper ports. Qt is installed separately at an exact version (6.8.3) by
CI and by the development container; see
[decisions.md](decisions.md#dependency-pinning).

| Package | Version | Commit | License |
|---|---|---|---|
| miniaudio | 0.11.25 | 9634bedb | MIT-0 or Unlicense |
| libsamplerate | 0.2.2 | c96f5e3d | BSD-2-Clause |
| Signalsmith Stretch | 1.4.0 | a670068d | MIT |
| Signalsmith Linear | 0.6.4 | de55e6a5 | MIT |
| RNNoise | 0.1.1 | 6cbfd53e | BSD-3-Clause |
| nlohmann/json | 3.12.0 | 55f93686 | MIT |
| Catch2 (tests only) | 3.9.1 | 644821ce | BSL-1.0 |
| Qt | 6.8.3 | v6.8.3 tag | LGPL-3 |

## Measurements

The sections below are filled from actual runs. Each table names the test
or tool that produced it.

### Pitch engine comparison

Produced by `vox_bench_pitch` (RelWithDebInfo, GCC 13, `-O2`). Input:
synthetic vowels with known pitch and formants (Rosenberg glottal pulses
through five formant resonators, 0.2 % jitter). "Envelope error vs ideal"
compares the LPC spectral envelope (order 44, 200 Hz to 5 kHz) of the output
with the same vowel synthesized directly at the target pitch; the
reference row shows the measurement floor (0 dB) and the "formants move"
rows show what an uncorrected shift costs (11 to 17 dB). CPU is the
fraction of one core of this container's 4 vCPU x86-64 machine.

Synthetic vowels /a/ /i/ /u/ at f0 100, 180, 260 Hz; 9 cases per row.

| Engine | Shift (st) | Mean pitch error (cents) | Max pitch error (cents) | Voiced frames | YIN aperiodicity | Envelope error vs ideal (dB) | Latency (ms) | CPU (x real time) |
|---|---|---|---|---|---|---|---|---|
| Reference: ideal shift | -12 | 0.5 | 0.7 | 100 % | 0.002 | 0.0 | 0.0 | 0.0000 |
| Reference: ideal shift | -5 | 0.1 | 0.2 | 100 % | 0.001 | 0.0 | 0.0 | 0.0000 |
| Reference: ideal shift | +5 | 0.1 | 0.2 | 100 % | 0.001 | 0.0 | 0.0 | 0.0000 |
| Reference: ideal shift | +12 | 0.2 | 0.4 | 100 % | 0.001 | 0.0 | 0.0 | 0.0000 |
| PSOLA (min f0 75 Hz) | -12 | 0.5 | 0.9 | 100 % | 0.001 | 5.2 | 30.2 | 0.0307 |
| PSOLA (min f0 75 Hz) | -5 | 0.5 | 0.9 | 100 % | 0.001 | 5.2 | 30.2 | 0.0327 |
| PSOLA (min f0 75 Hz) | +5 | 0.4 | 0.7 | 100 % | 0.001 | 5.7 | 30.2 | 0.0331 |
| PSOLA (min f0 75 Hz) | +12 | 0.5 | 1.0 | 100 % | 0.002 | 6.2 | 30.2 | 0.0337 |
| Spectral 20 ms, formants kept | -12 | 412.2 | 1200.0 | 87 % | 0.117 | 8.6 | 20.0 | 0.0145 |
| Spectral 20 ms, formants kept | -5 | 582.5 | 1443.3 | 89 % | 0.110 | 5.7 | 20.0 | 0.0165 |
| Spectral 20 ms, formants kept | +5 | 590.4 | 1716.9 | 93 % | 0.084 | 7.2 | 20.0 | 0.0165 |
| Spectral 20 ms, formants kept | +12 | 566.6 | 1297.4 | 80 % | 0.095 | 10.5 | 20.0 | 0.0168 |
| Spectral 20 ms, formants move | -12 | 311.1 | 1325.9 | 93 % | 0.058 | 16.0 | 20.0 | 0.0128 |
| Spectral 20 ms, formants move | -5 | 713.1 | 1889.3 | 93 % | 0.102 | 10.7 | 20.0 | 0.0118 |
| Spectral 20 ms, formants move | +5 | 648.5 | 1761.2 | 100 % | 0.049 | 11.9 | 20.0 | 0.0123 |
| Spectral 20 ms, formants move | +12 | 543.7 | 1289.6 | 98 % | 0.060 | 16.3 | 20.0 | 0.0123 |
| Spectral 30 ms, formants kept | -12 | 155.9 | 1209.1 | 88 % | 0.117 | 7.7 | 30.0 | 0.0154 |
| Spectral 30 ms, formants kept | -5 | 721.0 | 1905.1 | 81 % | 0.102 | 5.5 | 30.0 | 0.0166 |
| Spectral 30 ms, formants kept | +5 | 590.1 | 1861.3 | 89 % | 0.091 | 6.4 | 30.0 | 0.0163 |
| Spectral 30 ms, formants kept | +12 | 483.2 | 1808.4 | 88 % | 0.088 | 8.2 | 30.0 | 0.0173 |
| Spectral 30 ms, formants move | -12 | 165.8 | 1192.4 | 100 % | 0.032 | 16.2 | 30.0 | 0.0124 |
| Spectral 30 ms, formants move | -5 | 439.5 | 1200.0 | 79 % | 0.098 | 10.8 | 30.0 | 0.0135 |
| Spectral 30 ms, formants move | +5 | 315.8 | 1617.1 | 100 % | 0.037 | 11.6 | 30.0 | 0.0137 |
| Spectral 30 ms, formants move | +12 | 533.4 | 1202.2 | 100 % | 0.036 | 15.5 | 30.0 | 0.0139 |
| Spectral 43 ms, formants kept | -12 | 136.0 | 1183.9 | 99 % | 0.040 | 7.9 | 42.7 | 0.0180 |
| Spectral 43 ms, formants kept | -5 | 337.8 | 1200.0 | 72 % | 0.134 | 5.4 | 42.7 | 0.0185 |
| Spectral 43 ms, formants kept | +5 | 556.7 | 1890.2 | 81 % | 0.128 | 5.9 | 42.7 | 0.0197 |
| Spectral 43 ms, formants kept | +12 | 268.0 | 1202.0 | 100 % | 0.025 | 7.1 | 42.7 | 0.0194 |
| Spectral 43 ms, formants move | -12 | 10.8 | 22.9 | 100 % | 0.004 | 16.5 | 42.7 | 0.0148 |
| Spectral 43 ms, formants move | -5 | 169.8 | 787.2 | 81 % | 0.076 | 10.7 | 42.7 | 0.0154 |
| Spectral 43 ms, formants move | +5 | 4.5 | 9.6 | 100 % | 0.040 | 11.4 | 42.7 | 0.0181 |
| Spectral 43 ms, formants move | +12 | 3.9 | 5.9 | 100 % | 0.017 | 15.2 | 42.7 | 0.0158 |
| Spectral 120 ms, formants kept | -12 | 267.7 | 1203.4 | 100 % | 0.018 | 7.0 | 120.0 | 0.0166 |
| Spectral 120 ms, formants kept | -5 | 113.8 | 505.0 | 100 % | 0.006 | 4.6 | 120.0 | 0.0177 |
| Spectral 120 ms, formants kept | +5 | 268.3 | 1907.4 | 100 % | 0.027 | 4.5 | 120.0 | 0.0177 |
| Spectral 120 ms, formants kept | +12 | 3.5 | 9.5 | 100 % | 0.016 | 5.3 | 120.0 | 0.0173 |
| Spectral 120 ms, formants move | -12 | 1.8 | 4.7 | 100 % | 0.003 | 17.1 | 120.0 | 0.0129 |
| Spectral 120 ms, formants move | -5 | 3.9 | 9.6 | 100 % | 0.001 | 10.7 | 120.0 | 0.0139 |
| Spectral 120 ms, formants move | +5 | 2.1 | 3.9 | 100 % | 0.001 | 11.3 | 120.0 | 0.0147 |
| Spectral 120 ms, formants move | +12 | 1.0 | 2.9 | 100 % | 0.001 | 15.1 | 120.0 | 0.0140 |

How to read it:

* PSOLA: mean error 0.4 to 0.5 cents (max 1.0), output as periodic as the
  input (aperiodicity 0.001 to 0.002), envelope error 5 to 6 dB, 30.2 ms
  latency, 3 % of one core.
* Spectral at 20 to 43 ms: octave errors (mean errors of hundreds of
  cents) and noisier output on these voices.
* Spectral at 120 ms, formants moving: accurate (1 to 4 cents mean) and
  clean, which confirms the integration is correct; that mode is used for
  offline clip transposition.
* Spectral at 120 ms with formant compensation: still makes octave errors
  in some cases.

### Engine measurements

Filled in by milestone 3.

## Reproducing the numbers

All tools live in `tools/` and are built with the default presets (they
need the test support library, so `VOX_BUILD_TESTS` must be on).

```sh
cmake --preset release && cmake --build --preset release
./build/release/tools/vox_bench_pitch   # pitch engine comparison table
./build/release/tools/vox_bench_noise   # RNNoise suppression table
ctest --preset release                  # every functional test quoted above
```
