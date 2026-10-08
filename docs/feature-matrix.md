# Feature matrix

This document lists the observable features of the reference product (the
market-leading real-time voice changer and soundboard for Windows and macOS)
and states, for each one, whether Voxwright treats it as **must-have**,
**nice-to-have**, or **out of scope**, and how it is implemented.

Work here is clean-room: behavior was collected from the reference product's
public help-center articles ("Features: Free vs. PRO", "How to use the
Soundboard", "Keybinds", "Setting up", "Voices", "VoiceLab: How does it work?",
"How to change the microphone sample rate", "My voice falls and is cut out
(Noise gate)", "How to solve audio feedback or hearing yourself twice", "Audio
configuration error: Disconnected Input or Output device", "Audio Wizard
error: device is being used by another application in exclusive mode"), its
public marketing pages, the public documentation of third-party automation
tools that drive it (which enumerate its controllable states), and general
reviews. No binaries, installers, drivers, presets, sound packs, icons, or
branding were downloaded, inspected, or reused. Voice names, sounds, icons,
and visuals in Voxwright are original.

Legend: **M** = must-have, **N** = nice-to-have, **X** = out of scope.

Module names refer to the layout in [architecture.md](architecture.md):
`dsp/` (C++ DSP core), `plugins/` (effect registry and voice presets),
`engine/` (real-time graph), `devices/` (audio I/O), `app/` (Qt Quick UI).

## 1. Voice effects library

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Large catalog of ready-made voices (vendor claims 100 to 200+), grouped by theme (characters, creatures, devices, environments, gender/age, robots) | M | 54 original voices shipped as JSON presets in `plugins/voices/`, each a layered chain of effect blocks from the registry. Categories: Natural, Character, Creature, Machine, Device, Space, Place, Musical, Utility. Each is verified offline by pitch, loudness, latency, spectral, and distinctness measurements ([voices.md](voices.md)). |
| Live switching between voices with no dropout | M | Engine builds the new chain off the audio thread, hands it over through a lock-free mailbox, and crossfades old and new chains over 20 ms. |
| Per-voice quick sliders (pitch, bass, treble, "space"/reverb, background level) | M | Each preset declares 2 to 4 macro sliders that map to one or more block parameters with linear, exponential, or dB curves. Values persist per voice. |
| Voice changer master on/off | M | Engine bypass with a click-free crossfade; bound to a system hotkey and the tray menu. |
| Random voice | N | "Surprise me" button and hotkey pick a random voice from the current filter. |
| Voice categories, search, favorites | M | QML list with category chips, text search, favorites filter (see section 11). |
| Clean voice / "voice enhancer" preset | M | A "Clean" utility voice: high-pass, de-mud EQ, gentle compression, presence boost. |
| Neural voice conversion voices (vendor markets them as a separate premium line) | N | Separate ML milestone behind the `VOX_ENABLE_ML` build flag: ONNX Runtime, RVC-style model, benchmarked on CPU; the DSP path never depends on it. |
| Daily rotation of free voices, paid tier, voice store purchases | X | Business model features, not product capability. Every voice is available. |
| Online community voice catalog | X | Replaced by local import/export of `.voxvoice` files (section 2). |

## 2. Custom voice designer (sliders and parameters)

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Build a voice by stacking effect blocks (pitch, formant, reverb, delay, vocoder, distortion, EQ, robot effects, LFO, ambience) | M | Designer page in `app/`: block palette, ordered chain with drag reorder, per-block bypass and remove. Every block's parameters come from the effect descriptor metadata (name, unit, range, skew, default), so the UI is data-driven. |
| Hear changes live while editing | M | Parameter edits go through a lock-free SPSC queue to the running chain; values are smoothed per sample, so dragging a slider never clicks. Structural edits (add, remove, reorder) rebuild and crossfade. |
| Independent pitch and formant control | M | `dsp::PitchShifter` (pitch-synchronous overlap-add with grain resampling for formants, low latency) and `dsp::SpectralPitchShifter` (Signalsmith Stretch with formant compensation, higher quality, more latency). Both expose pitch (semitones and cents) and formant (semitones) independently. |
| Name, image, and save a custom voice; edit it later | M | Saved as JSON in the user data folder with name, category, accent color, and glyph icon. Built-in voices can be duplicated into editable copies. |
| Share custom voices | N | Export and import `.voxvoice` files (JSON). No online hub. |
| Expose chosen parameters as quick sliders on the voice card | N | Designer lets the user mark up to 4 parameters as macros. |
| 140+ separate effect modules | X | Count is not a goal. Voxwright ships about 25 general, well-tuned blocks whose parameters span the same space. |

## 3. Soundboard

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Multiple soundboards / folders (categories) | M | Boards are named categories in a sidebar; each holds an ordered grid of sound tiles. |
| Import user audio (MP3, WAV; vendor limit about 20 MB or 8 minutes) | M | Drag and drop or file dialog. Decoding by miniaudio (WAV, MP3, FLAC) and its bundled Vorbis decoder (OGG). Files are copied into the library folder. Limit 50 MB and 10 minutes with a specific error for each limit and for each decode failure. High-quality resampling to 48 kHz with libsamplerate (best sinc). |
| Loudness normalization on import | N | Optional: measure integrated loudness (ITU-R BS.1770 K-weighting, gated) and store a gain that brings each clip to -16 LUFS. |
| Per-sound hotkey | M | Global hotkey per sound (section 12). |
| Per-sound volume | M | dB gain per tile, smoothed. |
| Play modes: Play/Restart, Play/Stop, Play/Pause, Play/Overlap, hold-to-play loop | M | `engine::SoundboardPlayer` implements all five modes with a voice pool (32 voices), click-free start/stop fades. |
| Loop toggle | M | Per-sound loop flag with sample-accurate wrap. |
| Mute other sounds, stop other sounds, mute my voice while this plays | M | Per-sound flags handled in the player: duck other voices, stop them, or duck the processed mic signal. |
| "Mute for me" (others hear the sound, I do not) | M | Per-sound flag excludes the clip from the monitor bus. Global "sounds in my headphones" switch too. |
| Stop-all-sounds panic hotkey | M | System hotkey and button. |
| Slots that hold a voice instead of a sound | N | A tile can reference a voice; triggering it switches voice. |
| Built-in meme sounds | M | 24 original sounds synthesized from scratch by an internal tool (`tools/sounds/`): air horn, rimshot, sad trombone, applause, crickets, laser, boing, buzzer, ding, drum roll, whoosh, record scratch, cash register, explosion, slide whistle, alarm, and others. Original to this project, rendered at build time. No third-party clips. |
| Monthly content drops, online sound hub, mobile remote app | X | Require an online service. |
| Queue mode (feature request in the vendor's public feedback board) | X | Not in the reference product either. |

## 4. Background / ambient effects

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Voices carry background ambience (soundscape, music bed) that can be toggled | M | `dsp::Ambience` block synthesizes loops in real time (rain, wind, crowd murmur, engine hum, radio static, space station drone, cave drips, underwater bubbles, fire crackle, city traffic). Synthesized means no audio assets and no audible loop seam. |
| Global "background effects" on/off | M | Engine flag gates every ambience block with a fade; hotkey and tray toggle. |
| Background level per voice | M | Standard macro slider on voices that have ambience. |

## 5. Hear-myself monitor

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Hear-myself toggle (processed voice to headphones) | M | Monitor bus to the selected output device; toggle in the bottom bar, tray, and hotkey. |
| Monitor volume and per-channel mixer (voice, soundboard, hear-myself levels) | M | "Mixer" settings page with voice, soundboard, monitor, TTS, and background faders. |
| Automatic feedback detection that turns hear-myself off | N | `dsp::FeedbackDetector` watches for sustained narrow-band peaks with rising level (howl) and for high correlation between monitor output and mic input; when triggered the engine mutes the monitor and the UI explains why. Can be disabled in Advanced settings. |

## 6. Push-to-talk and push-to-mute

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Mute microphone toggle | M | Engine mute with fade; hotkey, tray, bottom bar. |
| Push-to-talk (hold key to transmit) | M | `engine::TransmitControl` modes: always on, push-to-talk, push-to-mute, toggle. Global hotkey with key-down and key-up events. 10 ms fade in, configurable release delay (default 150 ms) so word endings are not cut. |
| Push-to-mute (hold key to silence) | M | Same component, inverted. |
| Soundboard still audible during push-to-talk | M | Transmit control gates the voice path only; soundboard and TTS are mixed after it. |
| Censor beep (hold a key, voice replaced by a beep) | N | Hold hotkey crossfades the voice path to a 1 kHz tone. |

## 7. Noise reduction and gate

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Background noise reduction | M | RNNoise (BSD-3, 48 kHz, 10 ms frames) with a strength control that blends the suppressed and dry signal and applies a gain floor. |
| Noise gate with threshold slider ("filters the room when you stop talking") | M | `dsp::NoiseGate` with threshold, hysteresis, attack, hold, release, and range; meter shows threshold against input level. |
| Voice enhancement | N | Optional input stage: high-pass at 80 Hz, gentle compressor, de-esser. |
| Neural denoiser (DeepFilterNet) | N | Evaluated in the ML track; RNNoise stays default because of its 10 ms frame latency and low CPU. |

## 8. Mic and output device routing

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Choose input device (real microphone) and output device (headphones) | M | Device pickers fed by miniaudio enumeration (WASAPI on Windows, CoreAudio on macOS). |
| Choose the virtual-mic output (cable/driver) | M | Third picker, auto-selects a detected virtual cable. |
| Hot-plug handling and "disconnected device" error | M | Device change notifications trigger re-enumeration; a removed device produces a specific banner ("Microphone X was disconnected. Pick another input or reconnect it.") and the engine keeps running on silence. |
| "Device used by another application in exclusive mode" error | M | Mapped from the backend's error code to a specific message with the Windows setting that fixes it. |
| Exclusive mode option (Windows) | M | WASAPI exclusive toggle in Advanced settings; shared mode is the default. |
| Internal 48 kHz processing, any device rate accepted | M | Engine runs at 48 kHz; device streams at other rates go through streaming libsamplerate converters. Clock drift between capture and playback devices is absorbed by an adaptive resampling ratio driven by ring-buffer fill level. |
| Buffer size / latency setting | M | Advanced setting with a latency readout computed from the actual device periods. |
| Anti-popping | M | Always on: every start, stop, mute, and switch is a fade. |
| First-run audio wizard | M | First-run flow (section 13). |

## 9. Virtual microphone for Discord, Zoom, and games

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Virtual microphone device that chat apps select as input | M | Baseline: guided setup for VB-CABLE (Windows) and BlackHole 2ch (macOS). The engine writes to the cable's render side; chat apps record from its capture side. First-run flow detects the cable by device name, links to the official download page, re-scans, and runs a loopback test (plays a chirp into the cable, records it back, reports level and latency). |
| Vendor's own branded driver | N | Engine treats the virtual mic as an ordinary output device, so a first-party loopback driver drops in with no engine changes. Source for a macOS AudioServerPlugIn loopback driver lives in `drivers/macos/`; the Windows design is in `drivers/windows/README.md`. Neither can be built, signed, or tested in this sandbox. |
| Per-app setup guides (Discord, Zoom, Teams, OBS, games) | M | In-app help panel with the exact setting path per app. |

## 10. Text-to-speech

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Type text, speak it into the virtual mic in a chosen voice | N | Qt TextToSpeech `synthesize()` (Qt 6.6+) renders platform voices (SAPI/WinRT on Windows, AVSpeechSynthesizer on macOS) to PCM, which is resampled and mixed into the engine's TTS channel. Optional "apply my current voice effect" routes it through the active chain. |
| Text-to-song | X | No equivalent platform capability; not core. |

## 11. Presets and favorites

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Favorite voices | M | Star toggle, favorites filter, favorites submenu in tray. |
| Recently used voices | N | Last 8 voices persisted. |
| Saved slider state per voice | M | Macro values persisted per voice id. |
| Reset voice to defaults | M | Per-voice reset button. |

## 12. Global hotkeys

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Hotkey per voice | M | Assign from the voice card context menu. |
| Hotkey per sound | M | Assign from the sound tile. |
| System hotkeys (hear-myself, voice changer on/off, mute, stop all sounds, background on/off) | M | Hotkeys settings page. Also push-to-talk, push-to-mute, random voice, next/previous voice, censor beep. |
| Work while a game has focus | M | Windows: low-level keyboard hook on a dedicated thread (key-down and key-up, keys are not swallowed). macOS: Carbon `RegisterEventHotKey` (press and release events, no Accessibility permission needed). Linux is a development platform only and reports "global hotkeys unavailable". |
| Conflict detection | M | Binding store rejects duplicates with a message naming the existing owner. |
| Mouse buttons as hotkeys | N | Windows low-level mouse hook for X1/X2 buttons. |

## 13. Settings and first run

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Settings sections: Audio, Mixer, Hotkeys, General, Advanced | M | QML settings pages backed by a typed `AppSettings` store (QSettings). |
| Start with the operating system | M | Windows: `HKCU\...\CurrentVersion\Run` entry. macOS: `SMAppService.mainApp` (macOS 13+). Linux: XDG autostart file. |
| Start minimized | M | Launch flag added to the autostart entry. |
| Light and dark theme | N | Dark default, light alternative. |
| First-run flow | M | Pages: welcome, microphone permission (macOS), pick microphone with live meter, pick headphones with test tone, virtual cable detection and install guide with loopback test, chat app setup, done. |
| Reset all settings, open logs folder | M | Advanced page. |
| Accounts, login, telemetry, update checks | X | No account system, no network access by the app. |
| Localization | N | All strings wrapped in `qsTr`; English only at first. |

## 14. Tray behavior

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Tray icon with menu | M | `QSystemTrayIcon`: show/hide, voice changer, hear myself, mute, background effects, favorite voices submenu, stop all sounds, quit. |
| Minimize to tray / close to tray | M | General settings: "minimize to tray" and "close button hides to tray". First time the window hides, a tray notification explains where the app went. |
| Tray icon reflects state (muted, bypassed) | N | Icon variants for muted and voice-changer-off. |

## 15. Other observed features

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Local control API (WebSocket JSON) used by Stream Deck and automation tools | N | Not in the first release. The engine command interface is already message-based, so a local WebSocket server can be added in `app/` later. |
| Stream Deck, Loupedeck, OBS plugins | X | Third-party ecosystem integrations. |
| Mobile soundboard remote | X | Requires a companion app. |
| Real-time singing voices / music mode | N | "Musical" voices (hard tune, harmonizer, choir) cover the core of it. |

## Summary

* Must-have: 57 rows. Nice-to-have: 19 rows. Out of scope: 9 rows.
* Everything marked must-have is implemented in the milestones in
  [architecture.md](architecture.md). Status per item is tracked in
  [manual-test-checklist.md](manual-test-checklist.md) (what needs real
  hardware) and the test suite (what is verified automatically).
