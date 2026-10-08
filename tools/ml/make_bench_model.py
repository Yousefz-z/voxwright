#!/usr/bin/env python3
"""Writes a stand-in voice model for measuring CPU cost (not a voice).

    python3 tools/ml/make_bench_model.py build/ml/proxy.onnx

The graph has the shape of a typical neural voice converter, with random
weights: a convolutional content encoder like HuBERT's feature extractor
(seven 512-channel convolutions, 320x downsampling at 16 kHz) followed by a
HiFi-GAN-style decoder (transposed convolutions back up to 16 kHz with
residual blocks). It leaves out the transformer layers a real content
encoder adds, so real models cost more than this one. It exists only to
measure how the streaming block and ONNX Runtime behave under load.
"""

import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

rng = np.random.default_rng(1)
nodes: list[onnx.NodeProto] = []
weights: list[onnx.TensorProto] = []


def weight(name: str, shape: tuple[int, ...]) -> str:
    fan_in = int(np.prod(shape[1:]))
    w = rng.normal(0.0, 1.0 / np.sqrt(fan_in), size=shape).astype(np.float32)
    weights.append(numpy_helper.from_array(w, name))
    return name


def conv(x: str, name: str, cin: int, cout: int, k: int, stride: int = 1,
         dilation: int = 1) -> str:
    pad = (k - 1) * dilation // 2
    out = f"{name}_out"
    nodes.append(helper.make_node(
        "Conv", [x, weight(f"{name}_w", (cout, cin, k)), weight(f"{name}_b", (cout,))], [out],
        kernel_shape=[k], strides=[stride], dilations=[dilation], pads=[pad, pad]))
    return out


def up(x: str, name: str, cin: int, cout: int, factor: int) -> str:
    out = f"{name}_out"
    nodes.append(helper.make_node(
        "ConvTranspose",
        [x, weight(f"{name}_w", (cin, cout, 2 * factor)), weight(f"{name}_b", (cout,))],
        [out], kernel_shape=[2 * factor], strides=[factor],
        pads=[factor // 2, factor - factor // 2]))
    return out


def act(x: str, name: str) -> str:
    out = f"{name}_act"
    nodes.append(helper.make_node("LeakyRelu", [x], [out], alpha=0.1))
    return out


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    x = "audio_3d"
    nodes.append(helper.make_node("Unsqueeze", ["audio", "axis1"], [x]))
    weights.append(numpy_helper.from_array(np.array([1], dtype=np.int64), "axis1"))

    # Content encoder: strides 5,2,2,2,2,2,2 (x320), 512 channels.
    channels = 1
    for i, (k, s) in enumerate([(10, 5), (3, 2), (3, 2), (3, 2), (3, 2), (2, 2), (2, 2)]):
        x = act(conv(x, f"enc{i}", channels, 512, k, s), f"enc{i}")
        channels = 512
    x = conv(x, "proj", 512, 256, 1)

    # Decoder: x10, x8, x2, x2 back to 16 kHz with residual blocks.
    for i, (factor, cout) in enumerate([(10, 128), (8, 64), (2, 32), (2, 16)]):
        x = up(act(x, f"pre{i}"), f"up{i}", channels if i else 256, cout, factor)
        channels = cout
        for j, d in enumerate([1, 3, 5]):
            y = conv(act(x, f"res{i}{j}"), f"res{i}{j}", channels, channels, 3, dilation=d)
            out = f"res{i}{j}_sum"
            nodes.append(helper.make_node("Add", [x, y], [out]))
            x = out
    x = conv(act(x, "post"), "post", channels, 1, 7)
    nodes.append(helper.make_node("Tanh", [x], ["audio_tanh"]))
    nodes.append(helper.make_node("Squeeze", ["audio_tanh", "axis1"], ["audio_out"]))

    graph = helper.make_graph(
        nodes, "proxy",
        [helper.make_tensor_value_info("audio", TensorProto.FLOAT, [1, "samples"])],
        [helper.make_tensor_value_info("audio_out", TensorProto.FLOAT, [1, "out_samples"])],
        weights)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)],
                              producer_name="voxwright-bench")
    model.ir_version = 9
    for key, value in {"vox.name": "Benchmark proxy", "vox.input_rate": "16000",
                       "vox.output_rate": "16000"}.items():
        entry = model.metadata_props.add()
        entry.key = key
        entry.value = value
    onnx.checker.check_model(model)
    path = Path(sys.argv[1])
    path.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(path))
    params = sum(int(np.prod(w.dims)) for w in weights)
    print(f"{path}: {params / 1e6:.1f} M parameters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
