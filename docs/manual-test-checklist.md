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
| A16 | Both | Enable push-to-talk with the default 150 ms release delay. Speak while holding the key; release it mid-word. | Nothing is sent while the key is up; the end of the word after release is still sent; no clicks at either edge. (The global hotkey itself is checked in milestone 5.) | | | |
| A17 | Both | Turn noise reduction on next to a fan or air conditioner, then off. | The fan noise drops clearly while speech stays natural; toggling never clicks. | | | |

## Voices (milestone 2)

| # | Platform | Steps | Expected | Date | Tester | Result |
|---|---|---|---|---|---|---|
| V1 | Both | Listen to every built-in voice with a real male and a real female speaker, at normal and loud speaking levels. | Each voice sounds like its description, without warbling, clicks, or clipping. Note any voice that sounds wrong; the measurements in [voices.md](voices.md) were taken on synthetic speech only. | | | |
| V2 | Both | Low voices (Deep Baritone, Mountain Giant) with a speaker whose pitch goes below 75 Hz. | No octave jumps. If they occur, lower `minVoiceHz` and record the latency cost. | | | |
