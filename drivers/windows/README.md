# Windows virtual microphone driver (design)

**Status: design only. Not built, signed, installed, or tested.** Building
needs Visual Studio with the Windows Driver Kit, distributing needs an EV
code-signing certificate and Microsoft attestation signing, and testing
needs real Windows machines. None of these exist in the environment this
project was developed in. Until this driver exists, Voxwright uses
VB-CABLE ([decisions.md, D9](../../docs/decisions.md#d9-virtual-microphone)).

## Goal

Two audio endpoints that behave like one cable, installed by Voxwright's
own installer, so users do not need a third-party download:

| Endpoint | Direction | Who uses it |
|---|---|---|
| Voxwright Output | Render | Voxwright plays the changed voice into it. |
| Voxwright Virtual Microphone | Capture | Discord, Zoom, games, and OBS record from it. |

Whatever is rendered to the first comes out of the second with a fixed,
small delay. Voxwright's engine already treats the virtual microphone as
an ordinary output device and compensates the clock drift between devices
(see [architecture.md](../../docs/architecture.md#output-stages-and-clock-drift)),
so the engine needs no driver-specific code; `looksLikeVirtualCable()`
already recognizes the endpoint names.

## Architecture

A kernel-mode PortCls audio miniport driver using the WaveRT model, the
model Microsoft documents for virtual and software audio devices. The
public Windows driver samples (SysVAD, under the MS-PL license) show the
required structure; this driver would be written from the documented
interfaces rather than copied.

* **Device**: one root-enumerated device (`Root\VoxwrightMic`) created by
  the installer, with one adapter driver that registers two subdevices:
  a render WaveRT filter and a capture WaveRT filter, each with its
  topology filter.
* **Formats**: 48 kHz, stereo, 32-bit float and 16-bit PCM in both
  directions; 44.1 kHz offered for apps that insist. Voxwright opens the
  render side at 48 kHz float, which needs no conversion anywhere.
* **Loopback buffer**: a nonpaged ring buffer of 8192 frames (170 ms at
  48 kHz) shared by the two filters. The render stream's
  `IMiniportWaveRTStream` position advances from the system performance
  counter; a timer DPC every 1 ms copies frames between the render
  stream's cyclic buffer and the ring, and from the ring into the capture
  stream's cyclic buffer. If nothing renders, capture reads silence; if
  nothing captures, rendered audio is dropped after the ring fills, never
  blocking the render side.
* **Latency**: the copy period (1 ms) plus one capture packet; the
  endpoint reports it through `KSPROPERTY_RTAUDIO_HWLATENCY`.
* **Clock**: both streams derive positions from the same performance
  counter, so render and capture never drift against each other.
* **Power and removal**: D0/D3 transitions stop the timer and clear the
  ring; surprise removal completes outstanding streams so clients see a
  device-lost error rather than a hang.

## Packaging

* INF (`voxwrightmic.inf`) with `Class=MEDIA`, the two KS interface
  registrations, friendly names, and the root-enumerated hardware ID.
* Signed catalog (`voxwrightmic.cat`) from Microsoft attestation signing
  (Partner Center, EV certificate required). Test-signed builds only load
  with test signing enabled, which users should not be asked to do.
* The Inno Setup installer would run `pnputil /add-driver voxwrightmic.inf
  /install` and create the root device with SetupAPI
  (`SetupDiCreateDeviceInfo` and `DiInstallDevice`), elevated; uninstall
  removes the device and the driver package.

## Test plan (all manual, on Windows 10 21H2 and Windows 11)

1. Install, reboot-free: both endpoints appear in Sound settings with the
   right names and icons.
2. Loopback correctness: Voxwright's "Test the virtual microphone" check
   passes; a sine rendered at -6 dBFS is captured bit-exact (float) after
   the reported latency.
3. Latency: measure render-to-capture delay with a click; it matches
   `KSPROPERTY_RTAUDIO_HWLATENCY` within 1 ms.
4. Clients: Discord, Zoom, Teams, OBS, and a game record at the same time;
   each hears the voice.
5. Exclusive mode: a client opening the capture side in exclusive mode
   works; Voxwright's shared render keeps working.
6. Sleep and resume, fast user switching, and driver disable/enable while
   streaming: no crash, no stuck streams.
7. Driver Verifier and the Windows Hardware Lab Kit audio tests pass.
8. Uninstall removes both endpoints and leaves no service behind.
