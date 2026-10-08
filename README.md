# Voxwright

Voxwright is a real-time voice changer and soundboard for Windows 11 and
macOS. It runs your microphone through layered voice effects, mixes in
soundboard clips and background ambience, and plays the result into a
virtual microphone that Discord, Zoom, Teams, OBS, and games can use.

## Status

Development happens in milestones; each one is a commit.

| Milestone | Contents | State |
|---|---|---|
| 1 | C++20 DSP core, test-only offline render path, measurements | Done |
| 2 | Effect registry (21 blocks) and 54 voice presets, each measured | Done |
| 3 | Real-time engine, device routing, monitor, gate, noise reduction, push-to-talk | Planned |
| 4 | Qt Quick app shell with device selection and live voice switching | Planned |
| 5 | Soundboard with global hotkeys | Planned |
| 6 | Voice designer | Planned |
| 7 | Tray, settings, start on boot, first-run flow, installers | Planned |
| 8 | Optional neural voice conversion (behind `VOX_ENABLE_ML`) | Planned |

## Documentation

* [Feature matrix](docs/feature-matrix.md): every feature, its priority, and how it is built.
* [Architecture](docs/architecture.md): layers, threads, signal flow, and the measurements behind each choice.
* [Voices](docs/voices.md): the voice catalogue, how each voice was measured and tuned, and spectrograms.
* [Decisions](docs/decisions.md): scope decisions that must not be undone silently.
* [Contributing](CONTRIBUTING.md): toolchain setup and the exact checks CI runs.

## Building from source

Requirements and one-time setup (vcpkg at the pinned commit, Qt 6.8.3) are in
[CONTRIBUTING.md](CONTRIBUTING.md). Then:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Use the `windows` preset on Windows and the `macos` preset on macOS.

## License

See [docs/decisions.md](docs/decisions.md#d14-project-license) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
