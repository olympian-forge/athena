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
#include <string>
#include <vector>
#include "include/hardware/hardware.h"

class HardwareTest : public ::testing::Test
{
};

/* ---------------------------------------------------------------- Cpu --- */

TEST_F(HardwareTest, CpuStoresConstructorValues)
{
    hardware::Cpu cpu(8, "Intel(R) Core(TM) i7-5557U CPU @ 3.10GHz", 4);

    EXPECT_EQ(cpu.get_logical_cores(), 8u);
    EXPECT_EQ(cpu.get_physical_cores(), 4u);
    EXPECT_EQ(cpu.get_model_name(), "Intel(R) Core(TM) i7-5557U CPU @ 3.10GHz");
}

TEST_F(HardwareTest, CpuClassifiesIntelAsDefault)
{
    hardware::Cpu cpu(4, "Intel(R) Xeon(R) Gold 6248", 2);
    EXPECT_EQ(cpu.get_cpu_type(), hardware::CpuType::INTEL);
}

TEST_F(HardwareTest, CpuClassifiesUnknownVendorAsIntel)
{
    hardware::Cpu cpu(4, "Not available", 2);
    EXPECT_EQ(cpu.get_cpu_type(), hardware::CpuType::INTEL);
}

/**
 * set_cpu_type lowercases before matching, so each vendor keyword has to be
 * recognised regardless of the case the model string arrives in.
 */
TEST_F(HardwareTest, CpuClassifiesAmdKeywords)
{
    EXPECT_EQ(hardware::Cpu(4, "AMD Opteron", 2).get_cpu_type(), hardware::CpuType::AMD);
    EXPECT_EQ(hardware::Cpu(4, "amd opteron", 2).get_cpu_type(), hardware::CpuType::AMD);
    EXPECT_EQ(hardware::Cpu(4, "AMD Ryzen 9 5950X", 2).get_cpu_type(), hardware::CpuType::AMD);
    EXPECT_EQ(hardware::Cpu(4, "RYZEN 7", 2).get_cpu_type(), hardware::CpuType::AMD);
    EXPECT_EQ(hardware::Cpu(4, "EPYC 7742", 2).get_cpu_type(), hardware::CpuType::AMD);
}

/* ---------------------------------------------------------------- Gpu --- */

TEST_F(HardwareTest, GpuStoresConstructorValues)
{
    hardware::Gpu gpu(3, "NVIDIA GeForce RTX 4090", 24ULL * hardware::BYTES_PER_GB, 20ULL * hardware::BYTES_PER_GB);

    EXPECT_EQ(gpu.get_device_id(), 3);
    EXPECT_EQ(gpu.get_model_name(), "NVIDIA GeForce RTX 4090");
    EXPECT_EQ(gpu.get_total_memory_in_bytes(), 24ULL * hardware::BYTES_PER_GB);
    EXPECT_EQ(gpu.get_vram_size_in_bytes(), 20ULL * hardware::BYTES_PER_GB);
}

TEST_F(HardwareTest, GpuClassifiesNvidiaAsDefault)
{
    hardware::Gpu gpu(0, "NVIDIA GeForce RTX 4090", 0, 0);

    EXPECT_EQ(gpu.get_gpu_type(), hardware::GpuType::NVIDIA);
    EXPECT_TRUE(gpu.get_is_nvidia_gpu());
    EXPECT_FALSE(gpu.get_is_amd_gpu());
}

TEST_F(HardwareTest, GpuClassifiesAmdKeywords)
{
    hardware::Gpu amd(0, "AMD Instinct MI300", 0, 0);
    EXPECT_EQ(amd.get_gpu_type(), hardware::GpuType::AMD);
    EXPECT_TRUE(amd.get_is_amd_gpu());
    EXPECT_FALSE(amd.get_is_nvidia_gpu());

    hardware::Gpu radeon(1, "Radeon RX 7900 XTX", 0, 0);
    EXPECT_EQ(radeon.get_gpu_type(), hardware::GpuType::AMD);
    EXPECT_TRUE(radeon.get_is_amd_gpu());

    hardware::Gpu lower(2, "amd radeon", 0, 0);
    EXPECT_TRUE(lower.get_is_amd_gpu());
}

/* ---------------------------------------------------------------- Ram --- */

TEST_F(HardwareTest, RamStoresTotalSize)
{
    hardware::Ram ram(32ULL * hardware::BYTES_PER_GB);
    EXPECT_EQ(ram.get_total_size_in_bytes(), 32ULL * hardware::BYTES_PER_GB);
}

/* ---------------------------------------------------- OperatingSystem --- */

TEST_F(HardwareTest, OperatingSystemReportsNameAndWordSize)
{
    hardware::OperatingSystem os("TestOS");

    EXPECT_EQ(os.get_name(), "TestOS");
    EXPECT_EQ(os.get_is_64_bit(), sizeof(void *) == 8);
}

/**
 * os_type is decided by the compiling platform, not the name string, so the
 * expectation has to be written against the same preprocessor condition the
 * constructor uses.
 */
TEST_F(HardwareTest, OperatingSystemTypeMatchesBuildPlatform)
{
    hardware::OperatingSystem os("whatever");

#ifdef _WIN32
    EXPECT_EQ(os.get_os_type(), hardware::OperatingSystemType::WINDOWS);
    EXPECT_TRUE(os.get_is_windows());
#else
    EXPECT_EQ(os.get_os_type(), hardware::OperatingSystemType::LINUX);
    EXPECT_FALSE(os.get_is_windows());
#endif
}

/* ----------------------------------------------------------- HostInfo --- */

TEST_F(HardwareTest, HostInfoDefaultConstructorIsEmpty)
{
    hardware::HostInfo info;

    EXPECT_TRUE(info.get_cpus().empty());
    EXPECT_TRUE(info.get_gpus().empty());
    EXPECT_EQ(info.get_ram().get_total_size_in_bytes(), 0u);
    EXPECT_EQ(info.get_os().get_name(), "Unknown");
}

TEST_F(HardwareTest, HostInfoRoundTripsComponents)
{
    std::vector<hardware::Cpu> cpus;
    cpus.emplace_back(hardware::Cpu(16, "AMD EPYC 7742", 8));

    std::vector<hardware::Gpu> gpus;
    gpus.emplace_back(hardware::Gpu(0, "NVIDIA A100", hardware::BYTES_PER_GB, hardware::BYTES_PER_GB));

    hardware::Ram ram(64ULL * hardware::BYTES_PER_GB);
    hardware::OperatingSystem os("TestOS");

    hardware::HostInfo info(cpus, gpus, ram, os);

    ASSERT_EQ(info.get_cpus().size(), 1u);
    ASSERT_EQ(info.get_gpus().size(), 1u);
    EXPECT_EQ(info.get_cpus().at(0).get_model_name(), "AMD EPYC 7742");
    EXPECT_EQ(info.get_gpus().at(0).get_model_name(), "NVIDIA A100");
    EXPECT_EQ(info.get_ram().get_total_size_in_bytes(), 64ULL * hardware::BYTES_PER_GB);
    EXPECT_EQ(info.get_os().get_name(), "TestOS");
}

/* ---------------------------------------------------------- utilities --- */

/**
 * convert_bytes_to_gb deliberately adds one so a partially-filled gigabyte
 * still reports as a whole one; these expectations pin that rounding.
 */
TEST_F(HardwareTest, ConvertBytesToGbRoundsUp)
{
    EXPECT_EQ(hardware::convert_bytes_to_gb(0), 1u);
    EXPECT_EQ(hardware::convert_bytes_to_gb(hardware::BYTES_PER_GB), 2u);
    EXPECT_EQ(hardware::convert_bytes_to_gb(hardware::BYTES_PER_GB - 1), 1u);
    EXPECT_EQ(hardware::convert_bytes_to_gb(8ULL * hardware::BYTES_PER_GB), 9u);
}

TEST_F(HardwareTest, ConvertGbToBytesScalesExactly)
{
    EXPECT_EQ(hardware::convert_gb_to_bytes(0), 0u);
    EXPECT_EQ(hardware::convert_gb_to_bytes(1), hardware::BYTES_PER_GB);
    EXPECT_EQ(hardware::convert_gb_to_bytes(16), 16ULL * hardware::BYTES_PER_GB);
}

/**
 * The uint32_t parameter has to survive past the 255 GB that a uint8_t would
 * have silently truncated to zero.
 */
TEST_F(HardwareTest, ConvertGbToBytesHandlesLargeCapacities)
{
    EXPECT_EQ(hardware::convert_gb_to_bytes(512), 512ULL * hardware::BYTES_PER_GB);
    EXPECT_EQ(hardware::convert_gb_to_bytes(1024), 1024ULL * hardware::BYTES_PER_GB);
}

TEST_F(HardwareTest, ConvertRoundTripsWholeGigabytes)
{
    uint64_t bytes = hardware::convert_gb_to_bytes(4);
    EXPECT_EQ(hardware::convert_bytes_to_gb(bytes), 5u);
}

/* ------------------------------------------------------- host queries --- */

/**
 * detect_host_info delegates to the platform backend, so the assertions stay
 * on invariants that hold on any machine the suite runs on rather than on
 * this machine's specific hardware.
 */
TEST_F(HardwareTest, DetectHostInfoReportsUsableCpuAndOs)
{
    hardware::HostInfo info = hardware::detect_host_info();

    ASSERT_FALSE(info.get_cpus().empty());

    /* By value: get_cpus() returns a fresh vector, so a reference into it
     * would dangle the moment the temporary dies. */
    hardware::Cpu cpu = info.get_cpus().at(0);
    EXPECT_GT(cpu.get_logical_cores(), 0u);
    EXPECT_GT(cpu.get_physical_cores(), 0u);
    EXPECT_FALSE(cpu.get_model_name().empty());

    EXPECT_FALSE(info.get_os().get_name().empty());
    EXPECT_GT(info.get_ram().get_total_size_in_bytes(), 0u);
}

/**
 * Every detected GPU has to carry exactly one vendor flag, whichever backend
 * and whichever detection tier produced it.
 */
TEST_F(HardwareTest, DetectHostInfoGpusHaveExactlyOneVendor)
{
    hardware::HostInfo info = hardware::detect_host_info();

    for (const hardware::Gpu &gpu : info.get_gpus())
    {
        EXPECT_NE(gpu.get_is_amd_gpu(), gpu.get_is_nvidia_gpu());
        EXPECT_FALSE(gpu.get_model_name().empty());
    }
}

/**
 * 0 is the documented "no limit could be detected" answer, so the contract is
 * only that the value never exceeds what the machine physically has.
 */
TEST_F(HardwareTest, EffectiveCpuLimitIsZeroOrWithinMachineCapacity)
{
    uint32_t limit = hardware::get_effective_cpu_limit();

    if (limit > 0)
    {
        hardware::HostInfo info = hardware::detect_host_info();
        ASSERT_FALSE(info.get_cpus().empty());
        EXPECT_LE(limit, info.get_cpus().at(0).get_logical_cores());
    }
}
