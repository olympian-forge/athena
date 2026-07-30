/*
 *   Copyright (c) 2026 Ike
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>
#include "include/nn/nn.h"
#include "include/engine/engine.h"
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <thread>
#include <chrono>

using namespace chess;

class NNEvaluatorTest : public ::testing::Test {
};

TEST_F(NNEvaluatorTest, RequestEvaluationFallback) {
    nn::NN eval("dummy_does_not_exist", 0, 2, 2);
    Engine engine;

    auto f1 = eval.request_evaluation(engine.get_board_view());
    nn::Result res1 = f1.get();

    EXPECT_EQ(res1.value, 0.5);
    EXPECT_EQ(res1.policy.size(), nn::POLICY_SIZE);
}

/**
 * Sleeps briefly after the request to ensure batch_worker_loop hits the
 * early-return path in try_reload_model.
 */
TEST_F(NNEvaluatorTest, RequestEvaluationWithModel) {
    nn::NN eval("onnx/athena.onnx", 0, 2, 2);
    Engine engine;

    auto f1 = eval.request_evaluation(engine.get_board_view());
    nn::Result res1 = f1.get();

    /* Real model output: the tanh value head is converted to a win
     * probability in [0, 1]; exact values depend on the trained weights. */
    EXPECT_GE(res1.value, 0.0);
    EXPECT_LE(res1.value, 1.0);
    EXPECT_EQ(res1.policy.size(), nn::POLICY_SIZE);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

TEST_F(NNEvaluatorTest, CatchInvalidModelLoad) {
    std::ofstream out("bad_model.onnx");
    out << "garbage data";
    out.close();

    nn::NN eval("bad_model.onnx", 0, 1, 2);
    Engine engine;
    auto f1 = eval.request_evaluation(engine.get_board_view());
    nn::Result res1 = f1.get();

    EXPECT_EQ(res1.value, 0.5);

    std::filesystem::remove("bad_model.onnx");
}

/*
 * measure_latency_ms and the model-reload guards are reached through the
 * public surface: a real fixture, a deliberately wrong-shaped model, and an
 * empty file each drive a different branch.
 */

TEST_F(NNEvaluatorTest, MeasureLatencyWithoutModelReturnsFallback) {
    nn::NN eval("no_such_model_for_latency.onnx", 0, 2, 2);
    EXPECT_EQ(eval.measure_latency_ms(), 4);
}

TEST_F(NNEvaluatorTest, MeasureLatencyWithModelTimesARealBatch) {
    nn::NN eval("onnx/athena.onnx", 0, 2, 2);

    /* The worker loads the model lazily, so give the first reload a chance. */
    auto warm = eval.request_evaluation(Engine().get_board_view());
    warm.get();

    int latency = eval.measure_latency_ms();
    EXPECT_GE(latency, 0);
}

/**
 * A policy head narrower than POLICY_SIZE has to be rejected outright --
 * silently accepting it would misalign every batch item after the first.
 */
TEST_F(NNEvaluatorTest, WrongPolicyWidthIsRejected) {
    nn::NN eval("tests/dummy_wrong_policy.onnx", 0, 1, 2);

    auto f1 = eval.request_evaluation(Engine().get_board_view());
    nn::Result res1 = f1.get();

    /* Rejected batches fall back to the uniform prior. */
    EXPECT_EQ(res1.policy.size(), nn::POLICY_SIZE);
    EXPECT_GE(res1.value, 0.0);
    EXPECT_LE(res1.value, 1.0);
}

/* An existing but empty file must be skipped by the size guard. */
TEST_F(NNEvaluatorTest, EmptyModelFileIsSkipped) {
    const char *path = "empty_model.onnx";
    { std::ofstream create(path); }

    nn::NN eval(path, 0, 1, 2);
    auto f1 = eval.request_evaluation(Engine().get_board_view());
    nn::Result res1 = f1.get();

    EXPECT_EQ(res1.policy.size(), nn::POLICY_SIZE);
    std::filesystem::remove(path);
}

/**
 * A model file that exists but cannot be opened (no read permission) has to be
 * skipped like any other unreadable model, not crash the worker. Runs as a
 * normal user; root would bypass the permission check and this would not apply.
 */
TEST_F(NNEvaluatorTest, UnreadableModelFileIsSkipped) {
    const char *path = "unreadable_model.onnx";
    {
        std::ofstream create(path);
        create << "some content so the size guard passes";
    }
    ASSERT_EQ(chmod(path, 0000), 0);

    nn::NN eval(path, 0, 1, 2);
    auto f1 = eval.request_evaluation(Engine().get_board_view());
    nn::Result res1 = f1.get();

    EXPECT_EQ(res1.policy.size(), nn::POLICY_SIZE);

    chmod(path, 0644);
    std::filesystem::remove(path);
}
