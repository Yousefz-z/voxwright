# macOS virtual microphone driver

**Status: source written, never run.** `voxwright_mic.c` implements an
AudioServerPlugIn (a user-space driver that `coreaudiod` loads) from
Apple's documented interface in `CoreAudio/AudioServerPlugIn.h`. It was
written without access to a Mac. CI compiles it on macOS
(`.github/workflows/ci.yml`, job "macOS driver (compile only)"); nothing
has loaded, signed, or tested it. Until it is tested, Voxwright uses
BlackHole on macOS ([decisions.md, D9](../../docs/decisions.md#d9-virtual-microphone)).

## What it is

One device, "Voxwright Virtual Microphone", with an output stream that
Voxwright plays into and an input stream that chat apps record from, both
stereo 32-bit float at 44.1 or 48 kHz. Audio written for sample time *t*
is read back when the input reaches *t*, through a 16384-frame ring
buffer (341 ms at 48 kHz) indexed by sample time; frames not written in
the last ring length read as silence, so a stopped writer never repeats
old audio. The device clock is the host clock at the nominal rate, with a
zero time stamp every ring length.

## Build

```sh
cmake -S drivers/macos -B build/driver -G Ninja
cmake --build build/driver
```

## Install (for testing on a Mac you control)

```sh
sudo cp -R build/driver/VoxwrightMic.driver /Library/Audio/Plug-Ins/HAL/
sudo killall coreaudiod
```

Uninstall by deleting the bundle and restarting `coreaudiod` the same way.
For distribution the bundle must be signed with a Developer ID
Application certificate and notarized, and installed by a signed package;
none of that is set up.

## Test plan

1. Audio MIDI Setup lists the device with both directions, 44.1 and 48 kHz.
2. Voxwright's "Test the virtual microphone" check passes.
3. A sine played into the output is recorded bit-exact from the input in
   QuickTime Player or Audacity.
4. Discord, Zoom, and OBS record from it at the same time.
5. Switching the rate between 44.1 and 48 kHz while apps use it works.
6. Sleep and wake, and restarting `coreaudiod`, leave it working.
