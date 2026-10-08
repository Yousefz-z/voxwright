# Third-party notices

Voxwright uses the following components. Their license texts are installed
with the application (`licenses/` folder) and are available from the
projects' repositories at the pinned commits listed in
[docs/architecture.md](docs/architecture.md#dependencies).

| Component | Use | License |
|---|---|---|
| Qt 6.8.3 (Core, Gui, Qml, Quick, Quick Controls, Svg, Multimedia, TextToSpeech, Widgets) | User interface, tray, text-to-speech | LGPL-3.0, dynamically linked |
| miniaudio 0.11.25 | Audio device I/O and file decoding | MIT-0 or public domain (Unlicense) |
| stb_vorbis (bundled with miniaudio) | OGG Vorbis decoding | MIT or public domain |
| libsamplerate 0.2.2 | Sample-rate conversion | BSD-2-Clause |
| RNNoise 0.1.1 | Noise suppression | BSD-3-Clause |
| Signalsmith Stretch 1.4.0 and Signalsmith Linear 0.6.4 | Offline clip transposition | MIT |
| nlohmann/json 3.12.0 | Voice preset and settings files | MIT |
| Catch2 3.9.1 | Tests only, not shipped | BSL-1.0 |

Qt is used under the LGPL-3.0. The Qt libraries are shipped as separate
shared libraries that users can replace; the corresponding Qt sources are
available from https://code.qt.io/ at tag v6.8.3.

Virtual audio cables (VB-CABLE, BlackHole) are not bundled; users install
them from their official sites.

Builds with `VOX_ENABLE_ML=ON` (not the default installers) also include:

| Component | Use | License |
|---|---|---|
| ONNX Runtime 1.23.2 (Microsoft's prebuilt release) | Running neural voice models | MIT; its ThirdPartyNotices.txt is installed with it by vcpkg |

