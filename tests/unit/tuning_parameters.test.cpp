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

/*
 * The constructor reads athena.cfg from the working directory, so the config
 * path is exercised by writing that file rather than by injecting anything.
 * Each test removes it again so the detection path stays reachable too.
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <string>
#include "include/tuner/tuning_parameters.h"

class TuningParametersTest : public ::testing::Test
{
protected:
    void SetUp() override { std::remove(CONFIG_PATH); }

    void TearDown() override { std::remove(CONFIG_PATH); }

    static constexpr const char *CONFIG_PATH = "athena.cfg";

    void write_config(const std::string &contents)
    {
        std::ofstream cfg(CONFIG_PATH);
        ASSERT_TRUE(cfg.is_open());
        cfg << contents;
    }
};

/* --------------------------------------------------- detection path --- */

TEST_F(TuningParametersTest, WithoutFileLoadEverythingIsDerived)
{
    tuner::TuningParameters params(false);

    EXPECT_GT(params.get_search_threads(), 0);
    EXPECT_GT(params.get_batch_size(), 0);
    EXPECT_GT(params.get_pipeline_target(), 0);
    EXPECT_GT(params.get_batch_timeout_ms(), 0);
}

TEST_F(TuningParametersTest, DetectionPathPopulatesHostInfo)
{
    tuner::TuningParameters params(false);

    ASSERT_TRUE(params.get_host_info().has_value());
    EXPECT_FALSE(params.get_host_info()->get_cpus().empty());
}

/**
 * Search threads are held below the machine's logical core count, and the
 * cgroup/affinity clamp means they never exceed the effective allowance.
 */
TEST_F(TuningParametersTest, SearchThreadsStayWithinEffectiveAllowance)
{
    tuner::TuningParameters params(false);

    uint8_t threads = params.get_search_threads();
    EXPECT_GE(threads, 1);

    uint32_t effective = hardware::get_effective_cpu_limit();
    if (effective > 0)
    {
        EXPECT_LE(static_cast<uint32_t>(threads), effective);
    }
}

/* ------------------------------------------------------ config path --- */

TEST_F(TuningParametersTest, CompleteConfigIsLoadedVerbatim)
{
    write_config("SearchThreads=7\nBatchSize=321\nPipelineTarget=654\nBatchTimeoutMs=9\n");

    tuner::TuningParameters params(true);

    EXPECT_EQ(params.get_search_threads(), 7);
    EXPECT_EQ(params.get_batch_size(), 321);
    EXPECT_EQ(params.get_pipeline_target(), 654);
    EXPECT_EQ(params.get_batch_timeout_ms(), 9);
}

/**
 * A loaded config skips hardware detection entirely, so gpu_count is the
 * fixed fallback and no host info is cached.
 */
TEST_F(TuningParametersTest, LoadedConfigSkipsHardwareDetection)
{
    write_config("SearchThreads=2\nBatchSize=64\nPipelineTarget=128\nBatchTimeoutMs=3\n");

    tuner::TuningParameters params(true);

    EXPECT_EQ(params.get_gpu_count(), 1);
    EXPECT_FALSE(params.get_host_info().has_value());
}

/**
 * An old config missing a key leaves that value at zero, which fails the
 * completeness check and falls back to detection.
 */
TEST_F(TuningParametersTest, IncompleteConfigFallsBackToDetection)
{
    write_config("SearchThreads=7\nBatchSize=321\n");

    tuner::TuningParameters params(true);

    ASSERT_TRUE(params.get_host_info().has_value());
    EXPECT_GT(params.get_batch_timeout_ms(), 0);
}

TEST_F(TuningParametersTest, UnknownKeysAreIgnored)
{
    write_config("Nonsense=1\nSearchThreads=3\nBatchSize=32\nPipelineTarget=64\nBatchTimeoutMs=5\n");

    tuner::TuningParameters params(true);

    EXPECT_EQ(params.get_search_threads(), 3);
    EXPECT_EQ(params.get_batch_size(), 32);
}

TEST_F(TuningParametersTest, MissingConfigFileFallsBackToDetection)
{
    /* SetUp already removed athena.cfg, so the ifstream never opens. */
    tuner::TuningParameters params(true);

    ASSERT_TRUE(params.get_host_info().has_value());
    EXPECT_GT(params.get_search_threads(), 0);
}

/* ------------------------------------------------- accessors and ids --- */

TEST_F(TuningParametersTest, SettersRoundTrip)
{
    tuner::TuningParameters params(false);

    params.set_search_threads(11);
    params.set_batch_size(222);
    params.set_pipeline_target(333);
    params.set_batch_timeout_ms(44);

    EXPECT_EQ(params.get_search_threads(), 11);
    EXPECT_EQ(params.get_batch_size(), 222);
    EXPECT_EQ(params.get_pipeline_target(), 333);
    EXPECT_EQ(params.get_batch_timeout_ms(), 44);
}

/**
 * get_gpu_id answers 0 for any index the detected list does not contain,
 * which on a GPU-less machine is every index.
 */
TEST_F(TuningParametersTest, GpuIdIsZeroForOutOfRangeIndex)
{
    tuner::TuningParameters params(false);

    EXPECT_EQ(params.get_gpu_id(200), 0);
}

TEST_F(TuningParametersTest, GpuIdMatchesDetectedDevicesWhenPresent)
{
    tuner::TuningParameters params(false);

    ASSERT_TRUE(params.get_host_info().has_value());
    size_t gpu_count = params.get_host_info()->get_gpus().size();

    for (size_t index = 0; index < gpu_count; ++index)
    {
        uint8_t expected = static_cast<uint8_t>(
            params.get_host_info()->get_gpus().at(index).get_device_id());
        EXPECT_EQ(params.get_gpu_id(static_cast<uint8_t>(index)), expected);
    }
}

/**
 * Whatever the batch size ends up being, the pipeline target must stay large
 * enough to keep every search thread fed.
 */
TEST_F(TuningParametersTest, PipelineTargetCoversSearchThreads)
{
    tuner::TuningParameters params(false);

    EXPECT_GE(params.get_pipeline_target(), params.get_batch_size());
    EXPECT_GT(params.get_pipeline_target(), 0);
}

/* ------------------------------------------------ fabricated profiles --- */

/**
 * hardware::HostInfo is constructible directly, so a machine shape this host
 * does not have (multi-GPU, many-core) is built as real objects and handed to
 * the injecting constructor -- the same way board tests hand Board a FEN.
 */
class TuningProfileTest : public ::testing::Test
{
protected:
    static hardware::HostInfo make_host(uint32_t logical_cores, size_t gpu_count, uint32_t vram_gb)
    {
        std::vector<hardware::Cpu> cpus;
        cpus.emplace_back(hardware::Cpu(logical_cores, "Fabricated CPU", logical_cores / 2));

        std::vector<hardware::Gpu> gpus;
        for (size_t index = 0; index < gpu_count; ++index)
        {
            uint64_t vram = hardware::convert_gb_to_bytes(vram_gb);
            gpus.emplace_back(hardware::Gpu(static_cast<int>(index) + 3, "Fabricated GPU", vram, vram));
        }

        return hardware::HostInfo(cpus, gpus, hardware::Ram(hardware::convert_gb_to_bytes(64)),
                                  hardware::OperatingSystem("Fabricated OS"));
    }
};

/* ---- batch size ladder ---- */

TEST_F(TuningProfileTest, MultiGpuProfileDrivesSizing)
{
    tuner::TuningParameters params(make_host(32, 2, 24), 0);

    EXPECT_EQ(params.get_gpu_count(), 2);
    EXPECT_EQ(params.get_search_threads(), 30);

    /* Two 24 GB GPUs offer 2048, but 30 search threads only need 30 * 48,
     * and the smaller of the two is what gets used. */
    EXPECT_EQ(params.get_batch_size(), 30 * tuner::DEFAULT_CPU_BATCH_SIZE);
    EXPECT_GT(params.get_pipeline_target(), 0);
}

TEST_F(TuningProfileTest, InjectedProfileExposesGpuDeviceIds)
{
    tuner::TuningParameters params(make_host(8, 3, 16), 0);

    /* make_host numbers its fabricated devices from 3 upward. */
    EXPECT_EQ(params.get_gpu_id(0), 3);
    EXPECT_EQ(params.get_gpu_id(1), 4);
    EXPECT_EQ(params.get_gpu_id(2), 5);
    EXPECT_EQ(params.get_gpu_id(9), 0);
}

TEST_F(TuningProfileTest, InjectedLimitOverridesInjectedCoreCount)
{
    tuner::TuningParameters params(make_host(64, 0, 0), 4);

    EXPECT_EQ(params.get_search_threads(), 3);
    EXPECT_EQ(params.get_batch_size(), tuner::DEFAULT_CPU_BATCH_SIZE);
}

/*
 * Every branch below is reached by constructing TuningParameters with a
 * fabricated profile. Nothing reaches into private members and nothing in the
 * production API exists solely for these tests.
 */

/* Plenty of cores, so the GPU-derived batch is what wins the min(). */

TEST_F(TuningProfileTest, NoGpuUsesCpuBatchDefault)
{
    tuner::TuningParameters params(make_host(128, 0, 0), 0);
    EXPECT_EQ(params.get_batch_size(), tuner::DEFAULT_CPU_BATCH_SIZE);
}

/* 4 GB sits below both tiers, so 256 per GPU. */
TEST_F(TuningProfileTest, SmallVramUsesLowestPerGpuBatch)
{
    tuner::TuningParameters params(make_host(128, 1, 4), 0);
    EXPECT_EQ(params.get_batch_size(), 256);
}

TEST_F(TuningProfileTest, MidVramUsesMiddlePerGpuBatch)
{
    tuner::TuningParameters params(make_host(128, 1, 8), 0);
    EXPECT_EQ(params.get_batch_size(), 512);
}

TEST_F(TuningProfileTest, LargeVramUsesHighestPerGpuBatch)
{
    tuner::TuningParameters params(make_host(128, 1, 24), 0);
    EXPECT_EQ(params.get_batch_size(), 1024);
}

TEST_F(TuningProfileTest, BatchScalesWithGpuCount)
{
    tuner::TuningParameters params(make_host(128, 2, 24), 0);
    EXPECT_EQ(params.get_batch_size(), 2048);
}

/* With few threads the thread requirement caps the GPU-derived batch. */
TEST_F(TuningProfileTest, SearchThreadRequirementCapsGpuBatch)
{
    tuner::TuningParameters params(make_host(4, 1, 24), 0);
    EXPECT_EQ(params.get_batch_size(), 3 * tuner::DEFAULT_CPU_BATCH_SIZE);
}

/* ---- search thread ladder ---- */

TEST_F(TuningProfileTest, ManyCoresLeaveTwoSpare)
{
    EXPECT_EQ(tuner::TuningParameters(make_host(16, 0, 0), 0).get_search_threads(), 14);
    EXPECT_EQ(tuner::TuningParameters(make_host(5, 0, 0), 0).get_search_threads(), 3);
}

TEST_F(TuningProfileTest, FewCoresLeaveOneSpare)
{
    EXPECT_EQ(tuner::TuningParameters(make_host(4, 0, 0), 0).get_search_threads(), 3);
    EXPECT_EQ(tuner::TuningParameters(make_host(2, 0, 0), 0).get_search_threads(), 1);
}

TEST_F(TuningProfileTest, SingleCoreStillYieldsOneThread)
{
    EXPECT_EQ(tuner::TuningParameters(make_host(1, 0, 0), 0).get_search_threads(), 1);
}

/**
 * The container case: the machine reports 64 cores but the cgroup allows 8,
 * so sizing follows the allowance rather than the hardware.
 */
TEST_F(TuningProfileTest, EffectiveLimitClampsCoreCount)
{
    EXPECT_EQ(tuner::TuningParameters(make_host(64, 0, 0), 8).get_search_threads(), 6);
    EXPECT_EQ(tuner::TuningParameters(make_host(64, 0, 0), 2).get_search_threads(), 1);
}

TEST_F(TuningProfileTest, LimitLooserThanHardwareIsIgnored)
{
    EXPECT_EQ(tuner::TuningParameters(make_host(8, 0, 0), 32).get_search_threads(), 6);
}
