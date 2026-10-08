# Neural voice conversion (experimental)

Voxwright's voices are signal processing: pitch shifting, formant moves,
filters, and modulation. Neural voice conversion instead maps a voice onto
a target speaker with a trained model (RVC is the best-known open family).
This track adds the plumbing for such models behind a build flag. It ships
no model and no weights: a model is a file the user chooses, and they must
have the right to use it.

**Status.** Built only with `-DVOX_ENABLE_ML=ON`. The streaming block, the
model loader, and their tests run on Linux in this project's development
environment and in CI (job "Linux ML"). No trained voice model has been
tried: none could be downloaded here, and training one is out of scope.
Everything below about real models is a design, and the CPU figures come
from a stand-in model.

## How it fits

| Part | Where | What it does |
|---|---|---|
| ONNX Runtime 1.23.2 | `ports/onnxruntime-bin` | Microsoft's prebuilt CPU release for Windows x64, macOS (universal), and Linux x64, pinned by SHA-512 (vcpkg feature `ml`). |
| `NeuralModel` | `ml/src/neural_model.cpp` | Loads and checks a model, runs it on a window of audio. |
| `NeuralVoiceNode` | `ml/src/neural_voice.cpp` | An effect block ("Neural Voice") with Mix and Pitch settings that streams audio through the model on a worker thread. |
| `NeuralController` | `app/src/neural_controller.cpp` | Settings card: choose, show, and remove the model. |

Because the block is an ordinary effect in the registry, the voice
designer offers it like any other effect, and a voice can combine it with
the rest (for example neural conversion, then the radio filter).

## Model format

A Voxwright voice model is an ONNX file with:

| Name | Kind | Shape | Meaning |
|---|---|---|---|
| `audio` | input, float32 | [1, N] | Mono audio at `vox.input_rate`, in -1..1. |
| `pitch_shift` | input, float32, optional | [1] | The block's Pitch setting in semitones. |
| `audio_out` | output, float32 | [1, M] | Converted audio at `vox.output_rate`, covering the same time as the input. |

Metadata (`metadata_props`): `vox.name` (shown in Settings),
`vox.input_rate` and `vox.output_rate` in Hz (default 16000). The loader
refuses anything else with a message that names the problem.

An RVC voice is several networks (a content encoder such as HuBERT or
ContentVec, a pitch estimator, and the voice's synthesizer). To use one, it
would be exported as one ONNX graph with the inputs and outputs above, with
the pitch estimate computed inside the graph and `pitch_shift` added to it.
No such export exists in this repository.

## Streaming

The audio thread never waits for the model. Each block moves its samples
into a lock-free ring and takes converted samples out of another. A worker
thread waits for each new 100 ms of audio (the hop), gives the model that
hop plus the 100 ms before it (context), and joins consecutive outputs with
a 10 ms crossfade. The newest 10 ms of each output are held back
(lookahead) because resampling and many models are least accurate at the
edge.

The latency is fixed: hop + crossfade + lookahead + budget =
100 + 10 + 10 + 60 = **180 ms** with the defaults, where the budget is the
time a model run may take. A run that finishes later is late for some
samples; for those the block plays the input, delayed by the same latency,
so the voice continues instead of dropping out. The same happens with no
model loaded. The tests check that an identity model comes out sample
exact at that latency, that a model working at 16 kHz keeps level and
phase (125 dB SNR measured, 40 dB required), that the Pitch setting reaches the model, that a
model that never answers falls back to the plain voice, and that the audio
side allocates nothing.

180 ms is noticeable in conversation. A shorter hop lowers the latency
but raises the cost per second of audio, since the context is processed
again with every hop; `StreamingConfig` holds all four numbers.

## CPU measurements

`tools/ml/make_bench_model.py` writes a stand-in model with the shape of a
voice converter and random weights: a convolutional content encoder like
HuBERT's feature extractor followed by a HiFi-GAN-style decoder,
5.3 million parameters. It has no transformer layers, so a real converter
costs more. `vox_bench_ml` measured it on the development
machine (4 virtual CPUs):

| Threads | Mean per window (ms) | 95th percentile (ms) | Real-time factor |
|---|---|---|---|
| 1 | 17.5 | 29.7 | 0.17 |
| 2 | 14.2 | 17.5 | 0.14 |
| 4 | 14.6 | 18.9 | 0.15 |

Each window is 200 ms of audio (100 ms new). Running the block in real
time for 15 s with one model thread: 180 ms latency, 0.00 % of samples
late. (ONNX Runtime 1.23.2, release build, no other jobs running.)

A real-time factor below 1 means a run takes less time than the audio it
converts. The block needs each run to finish within the 60 ms budget, which
the stand-in meets with room to spare. A real converter is much larger
(HuBERT base alone has about 95 million parameters), so whether one fits
the budget on a CPU has to be measured with that model (checklist N2);
until then the track stays experimental.

## GPU

ONNX Runtime runs models on GPUs through execution providers; none is
enabled in this build, which uses the CPU release. The path to add one:

| Platform | Provider | What it needs |
|---|---|---|
| Windows, any DirectX 12 GPU | DirectML | The DirectML build of ONNX Runtime (a separate package) and `SessionOptions::AppendExecutionProvider("DML")`. |
| Windows and Linux, NVIDIA | CUDA | The GPU build of ONNX Runtime plus matching CUDA and cuDNN runtimes (large; users would install them). |
| macOS | CoreML | `AppendExecutionProvider("CoreML")`, available in the macOS release used here. |

Each would be chosen in `NeuralModel::load` with a CPU fallback when the
provider fails to start, and measured with `vox_bench_ml` before being
offered. None of this has been tried.

## What is untested

Everything that needs a real model or real hardware: conversion quality,
CPU cost of real converters, GPU providers, and the latency as heard in a
call. See the manual test checklist (N1 to N3).
