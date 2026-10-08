# Manual test checklist

Everything here needs real hardware, a real operating system session, or a
third-party driver, so none of it can be verified in the development
container or in CI. Nothing on this list has been run yet. Until a row has
a date, a tester, and a result, treat the feature as **unverified**.

The automated suites cover the same code paths with a fake audio backend
and synthetic signals (see [architecture.md](architecture.md#measurements));
this list checks what those cannot: drivers, real clocks, real voices, and
the operating system.

How to record a result: fill in the last three columns of the row, and
attach logs or measurements to the pull request that records them.

## Audio devices and engine (milestone 3)

| # | Platform | Steps | Expected | Date | Tester | Result |
|---|---|---|---|---|---|---|
| A1 | Windows 11 | Open the device lists with a USB microphone, onboard speakers, headphones, and VB-CABLE installed. | Every device appears once with its Windows name. "CABLE Input" is marked as a virtual cable. | | | |
| A2 | macOS 13+ | Same with the built-in microphone, AirPods, and BlackHole 2ch installed. | Every device appears; "BlackHole 2ch" is marked as a virtual cable. | | | |
| A3 | macOS | First launch on a clean account. | The system microphone permission prompt appears. Denying it shows the specific "microphone access denied" message with the System Settings path, not a generic error. | | | |
| A4 | Both | Select microphone, VB-CABLE/BlackHole as virtual mic, and headphones. Enable hear-myself. Speak. | Your voice is heard in the headphones and the input meter moves. | | | |
| A5 | Both | In Discord (or any chat app) select "CABLE Output" / "BlackHole 2ch" as its microphone. Speak with Clean Voice, then switch through ten voices while talking. A second account listens. | The listener hears every voice. Switching never clicks or drops out. | | | |
| A6 | Both | Measure the round trip with a hardware loopback: a cable from the headphone output to a line input (or the microphone through a speaker at 10 cm), Clean Voice, noise reduction off, 128-frame buffers. Use the impulse in `tools/` (milestone 7) or any loopback latency tool. | Record the measured latency and the engine's estimate from the latency readout. The difference is the driver and OS buffering. Target: under 20 ms with WASAPI exclusive on Windows and with CoreAudio on macOS. | | | |
| A7 | Windows 11 | Repeat A6 in shared mode. | Record it. Expect about 10 ms more than exclusive mode (the Windows audio engine period). | | | |
| A8 | Windows 11 | Start another app that holds the headphones in exclusive mode (for example a DAW with exclusive WASAPI), then select those headphones. | The specific "used by another application in exclusive mode" error, with the Windows setting that fixes it. | | | |
| A9 | Both | While running, unplug the USB microphone. Trigger a sound. Plug the microphone back in. | A "microphone disconnected" message names it. The sound still plays into the virtual mic. After replugging, the microphone works again without restarting the app. | | | |
| A10 | Both | While running, unplug the headphones (USB or Bluetooth). | A message names them; the virtual mic keeps working. Replugging restores them. | | | |
| A11 | Both | Change the system default output device while the app uses "System default". | Audio moves to the new default within a second, with no crash. | | | |
| A12 | Both | Use a microphone running at 44.1 kHz and a Bluetooth headset in hands-free mode (16 kHz) as the microphone. | Voice is clear and at the right pitch in both cases. | | | |
| A13 | Both | One-hour session: USB microphone and onboard (or separate USB) headphones, which run on different clocks. Speak occasionally; keep hear-myself on. | No clicks, dropouts, or growing delay. The underrun counter in the diagnostics view stays at 0. | | | |
| A14 | Both | On the slowest supported machine, pick the most expensive voice (see the processing cost table in architecture.md) with noise reduction on. | The processing load readout stays under 30 % and the audio has no dropouts. | | | |
| A15 | Both | With laptop speakers (not headphones) as the monitor, enable hear-myself and turn the volume up until it howls. | Within about a second hear-myself switches off by itself and the app explains why. | | | |
| A16 | Both | Enable push-to-talk with the default 150 ms release delay. Speak while holding the key; release it mid-word. | Nothing is sent while the key is up; the end of the word after release is still sent; no clicks at either edge. (The global talk key itself is row H3.) | | | |
| A17 | Both | Turn noise reduction on next to a fan or air conditioner, then off. | The fan noise drops clearly while speech stays natural; toggling never clicks. | | | |

## Application window (milestone 4)

| # | Platform | Steps | Expected | Date | Tester | Result |
|---|---|---|---|---|---|---|
| U1 | Windows 11 | Run at 100 %, 150 %, and 200 % display scaling. | Sharp text in Segoe UI, nothing clipped or overlapping, icons crisp. | | | |
| U2 | macOS | Run on a Retina and a non-Retina display. | Same as U1 with the system font. | | | |
| U3 | Both | Use the app with the keyboard only, then with Narrator (Windows) or VoiceOver (macOS). | Every toggle, slider, voice tile, and picker can be reached and is announced by name and state. | | | |
| U4 | Both | Change each device picker while speaking. | Audio resumes on the new device within a second, without a crash or stuck sound. | | | |
| U5 | Both | Run in a virtual machine without GPU acceleration, or over remote desktop. | The window renders (software renderer); voice icons stay inside the scrolling grid. | | | |
| U6 | Both | Change settings, quit, and start again. Then replace `settings.json` with garbage text and start again. | Settings come back as left. With the damaged file, a "Settings reset" banner appears and `settings.json.damaged` keeps the old content. | | | |
| U7 | Both | Start with no virtual cable installed; install VB-CABLE or BlackHole while the app runs. | The "No virtual microphone installed" banner links to the official page; after installing, the app picks the cable and says which microphone to choose in chat apps. | | | |

## Soundboard and hotkeys (milestone 5)

The Windows hook (`app/src/hotkeys/hotkeys_windows.cpp`) and the macOS
Carbon code (`app/src/hotkeys/hotkeys_macos.cpp`) are compiled by CI on
those platforms but have never run. Everything above them is tested with
simulated key presses.

| # | Platform | Steps | Expected | Date | Tester | Result |
|---|---|---|---|---|---|---|
| H1 | Both | Assign Ctrl+Alt+V to "Voice changer on or off". Focus another app (a browser, then a full-screen game) and press it. | The voice changer toggles each time; the other app also receives the key. | | | |
| H2 | Windows 11 | Repeat H1 in a game that runs as administrator, and in one with anti-cheat (for example a competitive shooter). | Record whether the key works. A low-level hook in a non-elevated app cannot see keys sent to an elevated window; if so, the Hotkeys page should say so (follow-up). | | | |
| H3 | Both | Push-to-talk with F9 as the talk key. Hold and release it while a chat app listens, with a game focused. | Voice is sent only while held, plus the release delay. Holding the key does not auto-repeat into presses. | | | |
| H4 | macOS 13+ | Assign hotkeys without granting any Accessibility or Input Monitoring permission. | Hotkeys work; no permission prompt appears. | | | |
| H5 | Both | Assign F2 in Voxwright, then assign the same key in another app that registers global hotkeys (OBS, Discord). | Record which app wins. On macOS, a key already registered by another app makes Voxwright report that the key is in use. | | | |
| H6 | Both | Give a voice a hotkey on the Voices page, switch to another voice, and press the key while a game has focus. | The voice switches with the usual 20 ms crossfade and the bottom bar shows its name. | | | |
| H7 | Windows 11 | Use a keyboard with a non-US layout (German QWERTZ, French AZERTY) and assign Ctrl+Z and Ctrl+1. | The key that is labeled Z or 1 on that keyboard triggers it. | | | |
| S1 | Both | Play each built-in sound into Discord while a second account listens, voice changer on. | Every sound is heard at a similar loudness, without distortion, and does not pass through the voice effect. | | | |
| S2 | Both | Drag WAV, MP3, FLAC, and OGG files from Explorer or Finder onto the soundboard. Then a 60 MB file, an 11-minute file, a renamed text file, and a file on a disconnected network drive. | The four files import and play. Each problem file gets its own message saying what is wrong and what to do; the others still import. | | | |
| S3 | Both | Set a sound to "Others only" and play it with hear-myself on. | The listener hears it; you do not. | | | |
| S4 | Both | Hold a "Play while held" sound's hotkey for two seconds while in a game. | It plays while held and stops on release without a click. | | | |
| S5 | Both | Quit with sounds and boards changed, start again; then corrupt `soundboards.json`. | Boards, sounds, and hotkeys come back. With the damaged file, a banner explains it and the default boards appear. | | | |

## Voices (milestone 2)

| # | Platform | Steps | Expected | Date | Tester | Result |
|---|---|---|---|---|---|---|
| V1 | Both | Listen to every built-in voice with a real male and a real female speaker, at normal and loud speaking levels. | Each voice sounds like its description, without warbling, clicks, or clipping. Note any voice that sounds wrong; the measurements in [voices.md](voices.md) were taken on synthetic speech only. | | | |
| V2 | Both | Low voices (Deep Baritone, Mountain Giant) with a speaker whose pitch goes below 75 Hz. | No octave jumps. If they occur, lower `minVoiceHz` and record the latency cost. | | | |
