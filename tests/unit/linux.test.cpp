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
 * Parsing for the Linux backend. Each parser takes data -- already-read lines,
 * or an open stream -- so the vendor tools and /proc need not exist here and
 * the production code carries nothing that exists only for these tests. The
 * thin layer that actually runs the commands and opens the files is excluded
 * from coverage instead; see the boundary section of linux.cpp.
 */

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>
#include "include/hardware/platform.h"
#include "src/hardware/linux_internal.h"

using hardware::platform::consider_cpu_limit;
using hardware::platform::parse_amd_gpus;
using hardware::platform::parse_cgroup_v1_quota;
using hardware::platform::parse_cgroup_v2_quota;
using hardware::platform::parse_cpuinfo;
using hardware::platform::parse_generic_gpus;
using hardware::platform::parse_nvidia_gpus;

class LinuxParseTest : public ::testing::Test
{
protected:
    /* Splits a blob into the line-per-entry shape get_command_stdout yields,
     * newline included, so the parsers see exactly what popen would give. */
    static std::vector<std::string> as_lines(const std::string &blob)
    {
        std::vector<std::string> lines;
        std::istringstream stream(blob);
        std::string line;
        while (std::getline(stream, line))
        {
            lines.push_back(line + "\n");
        }
        return lines;
    }
};

/* ------------------------------------------------------- nvidia-smi --- */

TEST_F(LinuxParseTest, NvidiaParsesIndexNameAndVram)
{
    std::vector<hardware::Gpu> gpus;
    parse_nvidia_gpus(as_lines("0, NVIDIA GeForce RTX 4090, 24576\n1, NVIDIA A100, 40960"), gpus);

    ASSERT_EQ(gpus.size(), 2u);
    EXPECT_EQ(gpus.at(0).get_device_id(), 0);
    EXPECT_EQ(gpus.at(0).get_model_name(), "NVIDIA GeForce RTX 4090");
    EXPECT_EQ(gpus.at(0).get_vram_size_in_bytes(), 24576ULL * 1024ULL * 1024ULL);
    EXPECT_TRUE(gpus.at(0).get_is_nvidia_gpu());
    EXPECT_EQ(gpus.at(1).get_device_id(), 1);
    EXPECT_EQ(gpus.at(1).get_model_name(), "NVIDIA A100");
}

TEST_F(LinuxParseTest, NvidiaIgnoresRowsWithoutTwoCommas)
{
    std::vector<hardware::Gpu> gpus;
    parse_nvidia_gpus(as_lines("garbage without commas"), gpus);
    EXPECT_TRUE(gpus.empty());
}

/* A non-numeric index makes stoi throw; the row is skipped, not fatal. */
TEST_F(LinuxParseTest, NvidiaSkipsUnparseableRow)
{
    std::vector<hardware::Gpu> gpus;
    parse_nvidia_gpus(as_lines("notanumber, Some GPU, 1024"), gpus);
    EXPECT_TRUE(gpus.empty());
}

TEST_F(LinuxParseTest, NvidiaHandlesNoOutput)
{
    std::vector<hardware::Gpu> gpus;
    parse_nvidia_gpus({}, gpus);
    EXPECT_TRUE(gpus.empty());
}

/* ---------------------------------------------------------- rocm-smi --- */

TEST_F(LinuxParseTest, AmdParsesCardRows)
{
    std::vector<hardware::Gpu> gpus;
    parse_amd_gpus(as_lines("device,name,vram\ncard0,Radeon RX 7900 XTX,21474836480"), gpus);

    ASSERT_EQ(gpus.size(), 1u);
    EXPECT_EQ(gpus.at(0).get_device_id(), 0);
    EXPECT_EQ(gpus.at(0).get_model_name(), "Radeon RX 7900 XTX");
    EXPECT_EQ(gpus.at(0).get_vram_size_in_bytes(), 21474836480ULL);
    EXPECT_TRUE(gpus.at(0).get_is_amd_gpu());
}

TEST_F(LinuxParseTest, AmdIgnoresRowsNotStartingWithCard)
{
    std::vector<hardware::Gpu> gpus;
    parse_amd_gpus(as_lines("header,name,vram"), gpus);
    EXPECT_TRUE(gpus.empty());
}

TEST_F(LinuxParseTest, AmdIgnoresCardRowWithoutTwoCommas)
{
    std::vector<hardware::Gpu> gpus;
    parse_amd_gpus(as_lines("card0 no commas here"), gpus);
    EXPECT_TRUE(gpus.empty());
}

TEST_F(LinuxParseTest, AmdSkipsUnparseableVram)
{
    std::vector<hardware::Gpu> gpus;
    parse_amd_gpus(as_lines("card0,Radeon,notanumber"), gpus);
    EXPECT_TRUE(gpus.empty());
}

/* -------------------------------------------------------------- lspci --- */

/**
 * The model name must come from the original line, not the lowercased copy
 * used for matching -- otherwise adapter names arrive mangled.
 */
TEST_F(LinuxParseTest, GenericPreservesNameCasing)
{
    std::vector<hardware::Gpu> gpus;
    parse_generic_gpus(as_lines("01:00.0 VGA compatible controller: NVIDIA Corporation GA104 [GeForce RTX 3070]"), gpus);

    ASSERT_EQ(gpus.size(), 1u);
    EXPECT_EQ(gpus.at(0).get_model_name(), "NVIDIA Corporation GA104 [GeForce RTX 3070]");
    EXPECT_EQ(gpus.at(0).get_vram_size_in_bytes(), hardware::convert_gb_to_bytes(1));
}

TEST_F(LinuxParseTest, GenericMatches3dControllerToo)
{
    std::vector<hardware::Gpu> gpus;
    parse_generic_gpus(as_lines("00:02.0 3D controller: Some Vendor Accelerator"), gpus);

    ASSERT_EQ(gpus.size(), 1u);
    EXPECT_EQ(gpus.at(0).get_model_name(), "Some Vendor Accelerator");
}

TEST_F(LinuxParseTest, GenericIgnoresNonGraphicsDevices)
{
    std::vector<hardware::Gpu> gpus;
    parse_generic_gpus(as_lines("00:1f.3 Audio device: Intel Corporation Sunrise Point-LP HD Audio"), gpus);
    EXPECT_TRUE(gpus.empty());
}

/* A VGA line with no ": " separator keeps the placeholder name. */
TEST_F(LinuxParseTest, GenericFallsBackToPlaceholderName)
{
    std::vector<hardware::Gpu> gpus;
    parse_generic_gpus(as_lines("vga"), gpus);

    ASSERT_EQ(gpus.size(), 1u);
    EXPECT_EQ(gpus.at(0).get_model_name(), "Generic Linux GPU");
}

TEST_F(LinuxParseTest, GenericAssignsSequentialDeviceIds)
{
    std::vector<hardware::Gpu> gpus;
    parse_generic_gpus(as_lines("01:00.0 VGA compatible controller: Card One\n"
                                "02:00.0 VGA compatible controller: Card Two"), gpus);

    ASSERT_EQ(gpus.size(), 2u);
    EXPECT_EQ(gpus.at(0).get_device_id(), 0);
    EXPECT_EQ(gpus.at(1).get_device_id(), 1);
}

/* ----------------------------------------------------- /proc/cpuinfo --- */

/**
 * Two sockets x two cores x two threads. Core id 0 on socket 0 and core id 0
 * on socket 1 are different cores, so the (physical id, core id) pair is what
 * has to be counted -- counting bare core ids would report 2 instead of 4.
 */
TEST_F(LinuxParseTest, CpuinfoCountsDistinctPhysicalCorePairs)
{
    std::string body;
    for (int package = 0; package < 2; ++package)
    {
        for (int core = 0; core < 2; ++core)
        {
            for (int thread = 0; thread < 2; ++thread)
            {
                body += "processor\t: 0\n";
                body += "model name\t: Test Xeon\n";
                body += "physical id\t: " + std::to_string(package) + "\n";
                body += "core id\t\t: " + std::to_string(core) + "\n\n";
            }
        }
    }

    std::istringstream input(body);
    std::vector<hardware::Cpu> cpus;
    parse_cpuinfo(input, 8, cpus);

    ASSERT_EQ(cpus.size(), 1u);
    EXPECT_EQ(cpus.at(0).get_logical_cores(), 8u);
    EXPECT_EQ(cpus.at(0).get_physical_cores(), 4u);
    EXPECT_EQ(cpus.at(0).get_model_name(), "Test Xeon");
}

/**
 * Kernels that publish no topology (most ARM boards) leave the pair set empty;
 * the count falls back to the logical count rather than to zero.
 */
TEST_F(LinuxParseTest, CpuinfoWithoutTopologyFallsBackToLogicalCount)
{
    std::istringstream input("processor\t: 0\nBogoMIPS\t: 108.00\n\n");
    std::vector<hardware::Cpu> cpus;
    parse_cpuinfo(input, 6, cpus);

    ASSERT_EQ(cpus.size(), 1u);
    EXPECT_EQ(cpus.at(0).get_logical_cores(), 6u);
    EXPECT_EQ(cpus.at(0).get_physical_cores(), 6u);
    EXPECT_EQ(cpus.at(0).get_model_name(), "Not available");
}

TEST_F(LinuxParseTest, CpuinfoZeroLogicalCoresUsesDefault)
{
    std::istringstream input("processor\t: 0\n");
    std::vector<hardware::Cpu> cpus;
    parse_cpuinfo(input, 0, cpus);

    ASSERT_EQ(cpus.size(), 1u);
    EXPECT_EQ(cpus.at(0).get_logical_cores(), 4u);
}

/* Non-numeric ids make stoi throw; the field is skipped, the parse continues. */
TEST_F(LinuxParseTest, CpuinfoSkipsMalformedFields)
{
    std::istringstream input("processor\t:\n"
                             "physical id\t: notanumber\n"
                             "core id\t\t: alsonotanumber\n"
                             "model name\t: Weird CPU\n"
                             "core id\t\t: 7\n");
    std::vector<hardware::Cpu> cpus;
    parse_cpuinfo(input, 2, cpus);

    ASSERT_EQ(cpus.size(), 1u);
    EXPECT_EQ(cpus.at(0).get_model_name(), "Weird CPU");
    EXPECT_EQ(cpus.at(0).get_physical_cores(), 1u);
}

TEST_F(LinuxParseTest, CpuinfoEmptyInputFallsBackToLogicalCount)
{
    std::istringstream input("");
    std::vector<hardware::Cpu> cpus;
    parse_cpuinfo(input, 3, cpus);

    ASSERT_EQ(cpus.size(), 1u);
    EXPECT_EQ(cpus.at(0).get_logical_cores(), 3u);
    EXPECT_EQ(cpus.at(0).get_physical_cores(), 3u);
}

/* ------------------------------------------------------ cgroup quotas --- */

TEST_F(LinuxParseTest, ConsiderCpuLimitKeepsTheTightestNonZero)
{
    uint32_t limit = 0;

    consider_cpu_limit(limit, 0);
    EXPECT_EQ(limit, 0u);

    consider_cpu_limit(limit, 8);
    EXPECT_EQ(limit, 8u);

    consider_cpu_limit(limit, 4);
    EXPECT_EQ(limit, 4u);

    consider_cpu_limit(limit, 9);
    EXPECT_EQ(limit, 4u);
}

TEST_F(LinuxParseTest, CgroupV2QuotaRoundsUpToWholeCores)
{
    std::istringstream input("250000 100000\n");
    uint32_t limit = 0;
    parse_cgroup_v2_quota(input, limit);
    EXPECT_EQ(limit, 3u);
}

TEST_F(LinuxParseTest, CgroupV2UnlimitedIsIgnored)
{
    std::istringstream input("max 100000\n");
    uint32_t limit = 0;
    parse_cgroup_v2_quota(input, limit);
    EXPECT_EQ(limit, 0u);
}

TEST_F(LinuxParseTest, CgroupV2MalformedQuotaIsIgnored)
{
    std::istringstream input("99999999999999999999999 100000\n");
    uint32_t limit = 0;
    parse_cgroup_v2_quota(input, limit);
    EXPECT_EQ(limit, 0u);
}

TEST_F(LinuxParseTest, CgroupV2EmptyStreamIsIgnored)
{
    std::istringstream input("");
    uint32_t limit = 0;
    parse_cgroup_v2_quota(input, limit);
    EXPECT_EQ(limit, 0u);
}

TEST_F(LinuxParseTest, CgroupV1QuotaRoundsUpToWholeCores)
{
    std::istringstream quota("150000\n");
    std::istringstream period("100000\n");
    uint32_t limit = 0;
    parse_cgroup_v1_quota(quota, period, limit);
    EXPECT_EQ(limit, 2u);
}

/* A quota of -1 is cgroup v1's "unlimited". */
TEST_F(LinuxParseTest, CgroupV1UnlimitedIsIgnored)
{
    std::istringstream quota("-1\n");
    std::istringstream period("100000\n");
    uint32_t limit = 0;
    parse_cgroup_v1_quota(quota, period, limit);
    EXPECT_EQ(limit, 0u);
}

TEST_F(LinuxParseTest, CgroupV1EmptyStreamsAreIgnored)
{
    std::istringstream quota("");
    std::istringstream period("");
    uint32_t limit = 0;
    parse_cgroup_v1_quota(quota, period, limit);
    EXPECT_EQ(limit, 0u);
}

/* ------------------------------------------------ public entry points --- */

/**
 * The detection entry points are the excluded I/O layer, but they still have
 * to return something coherent on whatever machine the suite runs on.
 */
TEST_F(LinuxParseTest, PublicEntryPointsReturnCoherentValues)
{
    std::vector<hardware::Cpu> cpus = hardware::platform::get_cpus();
    ASSERT_EQ(cpus.size(), 1u);
    EXPECT_GT(cpus.at(0).get_logical_cores(), 0u);
    EXPECT_LE(cpus.at(0).get_physical_cores(), cpus.at(0).get_logical_cores());

    EXPECT_GT(hardware::platform::get_ram().get_total_size_in_bytes(), 0u);
    EXPECT_EQ(hardware::platform::get_os().get_name(), "Linux");
    EXPECT_GT(hardware::platform::get_effective_cpu_limit(), 0u);

    for (const hardware::Gpu &gpu : hardware::platform::get_gpus())
    {
        EXPECT_FALSE(gpu.get_model_name().empty());
        EXPECT_NE(gpu.get_is_amd_gpu(), gpu.get_is_nvidia_gpu());
    }
}
