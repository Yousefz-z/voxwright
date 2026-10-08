#!/usr/bin/env python3
"""Writes the tiny ONNX models the neural voice tests use (ml/tests/models).

    python3 tools/ml/make_test_models.py ml/tests/models

Needs the onnx package. The models are committed (a few hundred bytes
each), so running the tests does not need Python.

Every model follows the contract in docs/ml.md: input "audio" [1, N]
float32 at metadata "vox.input_rate", output "audio_out" [1, M] at
"vox.output_rate", optional input "pitch_shift" [1] in semitones.
"""

import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

OPSET = 17
IR_VERSION = 9  # read by every ONNX Runtime since 1.15


def audio(name: str) -> onnx.ValueInfoProto:
    return helper.make_tensor_value_info(name, TensorProto.FLOAT, [1, "samples"])


def save(graph: onnx.GraphProto, path: Path, metadata: dict[str, str]) -> None:
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", OPSET)],
                              producer_name="voxwright-tests")
    model.ir_version = IR_VERSION
    for key, value in metadata.items():
        entry = model.metadata_props.add()
        entry.key = key
        entry.value = value
    onnx.checker.check_model(model)
    onnx.save(model, str(path))
    print(path)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    # Output equals input, at 48 kHz: the streaming must be sample exact.
    save(helper.make_graph([helper.make_node("Identity", ["audio"], ["audio_out"])],
                           "identity", [audio("audio")], [audio("audio_out")]),
         out / "identity_48k.onnx",
         {"vox.name": "Identity", "vox.input_rate": "48000", "vox.output_rate": "48000"})

    # Half the level, at 16 kHz in and out: resampling on both sides.
    half = numpy_helper.from_array(np.array([0.5], dtype=np.float32), "half")
    save(helper.make_graph([helper.make_node("Mul", ["audio", "half"], ["audio_out"])],
                           "gain", [audio("audio")], [audio("audio_out")], [half]),
         out / "half_16k.onnx",
         {"vox.name": "Half level", "vox.input_rate": "16000", "vox.output_rate": "16000"})

    # Takes the pitch input: output = audio * (1 + pitch_shift / 12), so a
    # test can see the value arrive (12 semitones doubles the level).
    twelfth = numpy_helper.from_array(np.array([1.0 / 12.0], dtype=np.float32), "twelfth")
    one = numpy_helper.from_array(np.array([1.0], dtype=np.float32), "one")
    pitch = helper.make_tensor_value_info("pitch_shift", TensorProto.FLOAT, [1])
    save(helper.make_graph([helper.make_node("Mul", ["pitch_shift", "twelfth"], ["scaled"]),
                            helper.make_node("Add", ["scaled", "one"], ["gain"]),
                            helper.make_node("Mul", ["audio", "gain"], ["audio_out"])],
                           "pitch", [audio("audio"), pitch], [audio("audio_out")],
                           [twelfth, one]),
         out / "pitch_gain_48k.onnx",
         {"vox.name": "Pitch as gain", "vox.input_rate": "48000", "vox.output_rate": "48000"})

    # Wrong names: the loader must say which input it expected.
    save(helper.make_graph([helper.make_node("Identity", ["x"], ["y"])],
                           "wrong", [audio("x")], [audio("y")]),
         out / "wrong_names.onnx", {"vox.name": "Wrong names"})
    return 0


if __name__ == "__main__":
    sys.exit(main())
