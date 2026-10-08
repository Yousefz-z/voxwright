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
| Build a voice by stacking effect blocks (pitch, formant, reverb, delay, vocoder, distortion, EQ, robot effects, LFO, ambience) | M | Voice designer page in `app/`: effect palette grouped by category, ordered chain with move up and move down buttons, per-block on/off and remove, up to 12 effects ([D29](decisions.md#d29-a-voice-holds-up-to-12-effects-and-4-quick-sliders)). Every block's controls come from the effect descriptor metadata (name, unit, range, scale, default, kind), so the UI is data-driven: sliders for ranges, switches for toggles, drop-downs for choices. |
| Hear changes live while editing | M | The draft plays as the active voice while it is edited. Setting changes go through the command queue and are smoothed per sample; structural edits (add, remove, move) rebuild the chain and crossfade over 20 ms. A test drags a bass setting across 60 audio blocks and removes a boosted equalizer while a tone plays, and finds no sample step above 1.5 times the steepest slope of the tone itself. Choosing another voice (grid or hotkey) pauses the preview until "Listen again". |
| Independent pitch and formant control | M | `dsp::PitchShifter` (pitch-synchronous overlap-add with grain resampling for formants, low latency) and `dsp::SpectralPitchShifter` (Signalsmith Stretch with formant compensation, higher quality, more latency). Both expose pitch (semitones and cents) and formant (semitones) independently. A test builds voices with the designer's operations and measures +12 semitones, and a formant shift that moves the spectrum while the pitch stays within 0.3 semitones. |
| Name, image, and save a custom voice; edit it later | M | Name, category, one of the category icons, accent color, and description. Saved as one JSON file per voice (the built-in format) in the user data folder's `voices/` ([D27](decisions.md#d27-the-users-voices-are-files-built-in-voices-are-read-only)); a damaged file is renamed `.damaged` and named in a notification. Built-in voices are edited as copies ("Name (mine)") that start where the user has the quick sliders. |
| Share custom voices | N | Export and import `.voxvoice` files (the same JSON). An import gets a new id and a unique name; each file that fails is named in one notification with the reason. No online hub. |
| Expose chosen parameters as quick sliders on the voice card | N | The slider button next to any continuous setting adds a quick slider (up to 4) that sweeps the setting's whole range (exponentially for frequencies and times) and starts at its current value. Removing one keeps the value it set. |
| 140+ separate effect modules | X | Count is not a goal. Voxwright ships 21 general, well-tuned blocks whose parameters span the same space. |

## 3. Soundboard

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Multiple soundboards / folders (categories) | M | Boards are named tabs above the sound grid; each holds an ordered grid of sound tiles. Boards can be added, renamed, and removed (never the last one). Saved in `soundboards.json` next to the settings, with the same atomic writes and damaged-file recovery. |
| Import user audio (MP3, WAV; vendor limit about 20 MB or 8 minutes) | M | Drag and drop or file dialog. Decoding by miniaudio (WAV, MP3, FLAC) and its bundled Vorbis decoder (OGG). Files are copied into the library folder. Limit 50 MB and 10 minutes with a specific error for each limit and for each decode failure. High-quality resampling to 48 kHz with libsamplerate (best sinc). |
| Loudness normalization on import | N | Optional: measure integrated loudness (ITU-R BS.1770 K-weighting, gated) and store a gain that brings each clip to -16 LUFS. |
| Per-sound hotkey | M | Global hotkey per sound, set in the sound's settings (section 12). |
| Per-sound volume | M | dB gain per tile, smoothed. |
| Play modes: Play/Restart, Play/Stop, Play/Pause, Play/Overlap, hold-to-play loop | M | `engine::SoundboardPlayer` implements all five modes with a voice pool (32 voices), click-free start/stop fades. |
| Loop toggle | M | Per-sound loop flag with sample-accurate wrap. |
| Mute other sounds, stop other sounds, mute my voice while this plays | M | Per-sound flags handled in the player: duck other voices, stop them, or duck the processed mic signal. |
| "Mute for me" (others hear the sound, I do not) | M | Per-sound flag excludes the clip from the monitor bus. Global "sounds in my headphones" switch too. |
| Stop-all-sounds panic hotkey | M | System hotkey and button. |
| Slots that hold a voice instead of a sound | N | A tile can reference a voice; triggering it switches voice. |
| Built-in meme sounds | M | 18 original sounds synthesized from code in `plugins/src/sound_pack.cpp` when the app starts (oscillators, noise, filters, envelopes, a small reverb): stadium horn, applause, ta-da, deflate, rimshot, drum roll, fanfare, laser, boing, whoosh, explosion, glitch, crickets, heartbeat, wrong answer, right answer, alarm, censor bleep. Each is normalized to -18 LUFS with peaks under -1 dBFS. No recordings and no third-party clips ([decisions.md](decisions.md#d24-built-in-sounds-are-rendered-at-startup)). |
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
| Monitor volume and per-channel mixer (voice, soundboard, hear-myself levels) | M | "Monitoring and mix" card on the Audio page: voice, sounds, text-to-speech, and headphone levels. Background level is a macro on each voice that has ambience. |
| Automatic feedback detection that turns hear-myself off | N | While hear-myself is on, `dsp::FeedbackDetector` watches the microphone for a loud, narrow spectral peak that holds its frequency longer than speech does (howl). The engine then fades the monitor out, turns hear-myself off, and reports the frequency so the UI can explain why (test: a 2.5 kHz howl is caught and reported within 30 Hz). Can be disabled in Advanced settings. |

## 6. Push-to-talk and push-to-mute

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Mute microphone toggle | M | Engine mute with fade; hotkey, tray, bottom bar. |
| Push-to-talk (hold key to transmit) | M | `engine::TransmitControl` modes: always on, push-to-talk, push-to-mute; mute is a separate toggle on top. Global hotkey with key-down and key-up events. 10 ms fade in, 20 ms fade out, configurable release delay (default 150 ms) after a key release so word endings are not cut. |
| Push-to-mute (hold key to silence) | M | Same component, inverted. |
| Soundboard still audible during push-to-talk | M | Transmit control gates the voice path only; soundboard and TTS are mixed after it. |
| Censor beep (hold a key, voice replaced by a beep) | N | Hold hotkey crossfades the voice path to a 1 kHz tone. |

## 7. Noise reduction and gate

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Background noise reduction | M | RNNoise (BSD-3, 48 kHz, 10 ms frames) with a strength control that blends the suppressed signal with the equally delayed dry signal. RNNoise runs continuously so it is ready the moment it is switched on; switching crossfades over 20 ms, and when off it adds no latency (its 20 ms apply only while it is on). |
| Noise gate with threshold slider ("filters the room when you stop talking") | M | `dsp::NoiseGate` with threshold, hysteresis, attack, hold, release, and range; meter shows threshold against input level. |
| Voice enhancement | N | Optional input stage: high-pass at 80 Hz, gentle compressor, de-esser. |
| Neural denoiser (DeepFilterNet) | N | Evaluated in the ML track; RNNoise stays default because of its short latency (20 ms) and low CPU. |

## 8. Mic and output device routing

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Choose input device (real microphone) and output device (headphones) | M | Device pickers fed by miniaudio enumeration (WASAPI on Windows, CoreAudio on macOS). |
| Choose the virtual-mic output (cable/driver) | M | Third picker, auto-selects a detected virtual cable. |
| Hot-plug handling and "disconnected device" error | M | Device notifications are queued to the control thread. A lost device produces a `DeviceLost` event naming the device and its role (for the banner: "Microphone X was disconnected. Pick another input or reconnect it."); the engine keeps running on the remaining devices (without a microphone, the virtual-mic output drives processing so sounds and speech still play) and reopens the device when it reappears (`DeviceRestored`). |
| "Device used by another application in exclusive mode" error | M | Mapped from the backend's error code to a specific message with the Windows setting that fixes it. |
| Exclusive mode option (Windows) | M | WASAPI exclusive toggle in Advanced settings; shared mode is the default. |
| Internal 48 kHz processing, any device rate accepted | M | Engine runs at 48 kHz; devices open at their native rate (8 to 384 kHz) and go through streaming libsamplerate converters. Clock drift between capture and playback devices is absorbed by a PI controller that trims each output's conversion ratio from its buffer fill (simulated: +-300 ppm at 44.1, 48, and 96 kHz with 1.5 ms scheduling jitter, no dropouts in 90 s, 97 dB tone-to-noise). |
| Buffer size / latency setting | M | Advanced setting with a latency readout computed from the actual device periods. |
| Anti-popping | M | Always on: every start, stop, mute, and switch is a fade. |
| First-run audio wizard | M | First-run flow (section 13). |

## 9. Virtual microphone for Discord, Zoom, and games

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Virtual microphone device that chat apps select as input | M | Baseline: guided setup for VB-CABLE (Windows) and BlackHole 2ch (macOS). The engine writes to the cable's render side; chat apps record from its capture side. The setup guide detects the cable by device name, links to the official download page, re-scans, and runs a loopback check: the engine plays a 0.3 s chirp into the cable while Voxwright records the cable's recording side, and a normalized cross-correlation says whether the chirp arrived, nothing arrived, or something else arrived ([D33](decisions.md#d33-the-virtual-microphone-check-listens-where-chat-apps-listen)). |
| Vendor's own branded driver | N | Engine treats the virtual mic as an ordinary output device, so a first-party loopback driver drops in with no engine changes. Source for a macOS AudioServerPlugIn loopback driver lives in `drivers/macos/` (CI compiles it; it has never been loaded or tested); the Windows design is in `drivers/windows/README.md`. Neither can be signed or tested in this environment. |
| Per-app setup guides (Discord, Zoom, Teams, OBS, games) | M | Settings page card listing where each app keeps its microphone setting, with the exact device name to choose. The menu paths are on the manual checklist (T10) because apps move them. |

## 10. Text-to-speech

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Type text, speak it into the virtual mic in a chosen voice | N | Text to speech panel under the soundboard. Qt TextToSpeech `synthesize()` renders a system voice (SAPI or WinRT on Windows, AVSpeechSynthesizer on macOS, speech-dispatcher or flite on Linux) to PCM, which is converted to mono, resampled to 48 kHz, and played on the engine's speech channel; "Through my voice effect" routes it through the active voice. A test speaks a sentence with flite and measures it at the virtual cable. |
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
| Hotkey per voice | M | Set in the current-voice panel on the Voices page; the key switches to that voice from any app. |
| Hotkey per sound | M | Set in the sound's settings, opened from the pencil on its tile. |
| System hotkeys (hear-myself, voice changer on/off, mute, stop all sounds, background on/off) | M | Hotkeys settings page. Also push-to-talk, push-to-mute, random voice, next/previous voice, censor beep. |
| Work while a game has focus | M | Windows: low-level keyboard hook on a dedicated thread (key-down and key-up, keys are not swallowed). macOS: Carbon `RegisterEventHotKey` (press and release events, no Accessibility permission needed). Linux is a development platform only and reports "global hotkeys unavailable". |
| Conflict detection | M | A key already in use is refused with a message naming its owner (an action, a voice, or a sound). Saved keys that conflict at startup are dropped with a notification. |
| Mouse buttons as hotkeys | N | Windows low-level mouse hook for X1/X2 buttons. |

## 13. Settings and first run

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Settings sections: Audio, Mixer, Hotkeys, General, Advanced | M | Audio page (devices, input processing, mix, latency, buffer), Hotkeys page, and Settings page (startup and tray, virtual microphone check, app setup, setup guide, advanced). |
| Settings storage | M | A typed `AppSettings` struct saved as JSON by `SettingsStore`: atomic writes, and a damaged file is set aside and reported instead of crashing or silently resetting ([decisions.md](decisions.md#d22-settings-are-a-json-file)). |
| Start with the operating system | M | Windows: a value in `HKCU\...\CurrentVersion\Run` (removed by the uninstaller). macOS: a LaunchAgent in `~/Library/LaunchAgents` ([D31](decisions.md#d31-start-at-login-on-macos-uses-a-launchagent)). Linux: an XDG autostart file. All three are written by the same tested code with per-system quoting. |
| Start minimized | M | "Start hidden in the tray" adds `--minimized` to the sign-in entry; the window then stays hidden until opened from the tray. |
| Light and dark theme | N | Dark default, light alternative. |
| First-run flow | M | Setup guide on first start: welcome, microphone with live meter (macOS asks for microphone permission when audio starts), virtual microphone with download link, re-scan, and the loopback check, headphones with hear-myself, then where to go next. It can be run again from Settings. |
| Reset all settings, open logs folder | M | Settings page, Advanced: "Reset all settings" (after a confirmation; the user's voices and soundboards are kept, and a restart is offered) and "Open the settings folder". Voxwright writes no log files, so there is no logs folder. |
| Accounts, login, telemetry, update checks | X | No account system, no network access by the app. |
| Localization | N | All strings wrapped in `qsTr`; English only at first. |

## 14. Tray behavior

| Feature (observed behavior) | Priority | Implementation |
|---|---|---|
| Tray icon with menu | M | `QSystemTrayIcon` ([D30](decisions.md#d30-the-tray-uses-qt-widgets)): open, voice changer, hear myself, background effects, mute, favorite voices submenu, stop all sounds, quit. Checkmarks follow the switches wherever they change. |
| Minimize to tray / close to tray | M | Settings: "Keep running when the window is closed" (on by default where a tray exists). The first time the window hides, a tray notification says where the app went. Minimizing keeps the normal taskbar or Dock behavior. |
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
