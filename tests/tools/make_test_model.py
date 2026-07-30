#!/usr/bin/env python3
#   Copyright (c) 2026 Ike
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Generate the tiny ONNX fixtures the unit tests load.

These are NOT networks -- they have no weights and know nothing about chess.
They exist so nn.test.cpp can reach code paths a missing file cannot:

  tests/dummy.onnx               a loadable model with the correct policy width,
                                 so the "model loaded successfully" path runs
  tests/dummy_wrong_policy.onnx  the same graph with a deliberately narrow
                                 policy head, so the tensor-size check that
                                 guards against a misaligned batch is exercised

A real network from init_model.py is ~82 MB, far too large to commit, and these
are generated rather than checked in so no binary artifacts live in the repo.
Both carry the real input/output signature in a few hundred bytes by deriving
correctly-shaped zero outputs from the input's batch dimension.

This lives under tests/ rather than tools/ because it is test scaffolding, not
something a user of the engine ever runs -- tools/ is for init_model.py and the
other operator-facing scripts.

scripts/test.sh runs this automatically before building. To do it by hand:
  python3 tests/tools/make_test_model.py
"""

import argparse
import os

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

# Must match include/nn/nn.h POLICY_SIZE and the names in NN::evaluate_batch.
POLICY_SIZE = 4672
INPUT_NAME = "input"
POLICY_NAME = "policy"
VALUE_NAME = "value"
OPSET_VERSION = 14
IR_VERSION = 10

# Narrow enough that NN::evaluate_batch's exact-size check must reject it.
WRONG_POLICY_SIZE = 16


def build_model(policy_size, graph_name):
    """Shape -> Slice -> Concat -> ConstantOfShape, twice: batch-sized zero
    outputs with no weight tensors, so the whole model stays tiny."""
    nodes = [
        helper.make_node("Shape", [INPUT_NAME], ["input_shape"]),
        helper.make_node("Slice", ["input_shape", "zero", "one", "zero"], ["batch_dim"]),
        helper.make_node("Concat", ["batch_dim", "policy_width"], ["policy_shape"], axis=0),
        helper.make_node("ConstantOfShape", ["policy_shape"], [POLICY_NAME],
                         value=helper.make_tensor("p", TensorProto.FLOAT, [1], [0.0])),
        helper.make_node("Concat", ["batch_dim", "one"], ["value_shape"], axis=0),
        helper.make_node("ConstantOfShape", ["value_shape"], [VALUE_NAME],
                         value=helper.make_tensor("v", TensorProto.FLOAT, [1], [0.0])),
    ]

    initializers = [
        numpy_helper.from_array(np.array([0], dtype=np.int64), "zero"),
        numpy_helper.from_array(np.array([1], dtype=np.int64), "one"),
        numpy_helper.from_array(np.array([policy_size], dtype=np.int64), "policy_width"),
    ]

    graph = helper.make_graph(
        nodes,
        graph_name,
        [helper.make_tensor_value_info(INPUT_NAME, TensorProto.FLOAT, ["batch_size", 14, 8, 8])],
        [helper.make_tensor_value_info(POLICY_NAME, TensorProto.FLOAT, ["batch_size", policy_size]),
         helper.make_tensor_value_info(VALUE_NAME, TensorProto.FLOAT, ["batch_size", 1])],
        initializer=initializers,
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", OPSET_VERSION)])
    model.ir_version = IR_VERSION
    onnx.checker.check_model(model)
    return model


def write(model, path):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    onnx.save_model(model, path, save_as_external_data=False)
    print(f"wrote {path} ({os.path.getsize(path)} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Generate the tiny ONNX test fixtures")
    parser.add_argument("--out-dir", default="tests", help="output directory (default: tests)")
    args = parser.parse_args()

    write(build_model(POLICY_SIZE, "athena_test_fixture"),
          os.path.join(args.out_dir, "dummy.onnx"))
    write(build_model(WRONG_POLICY_SIZE, "athena_wrong_policy_fixture"),
          os.path.join(args.out_dir, "dummy_wrong_policy.onnx"))


if __name__ == "__main__":
    main()
