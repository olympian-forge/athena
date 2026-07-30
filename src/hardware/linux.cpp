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

#include "include/logger/logger.h"
#include "include/hardware/platform.h"
#include "src/hardware/linux_internal.h"
#include "include/hardware/posix.h"
#include <cctype>
#include <fstream>
#include <sched.h>
#include <set>
#include <sys/sysinfo.h>
#include <thread>

namespace hardware::platform
{
    constexpr uint32_t DEFAULT_CPU_CORES = 4;
    constexpr uint8_t DEFAULT_GPU_VRAM_IN_GB = 1;
    constexpr uint8_t DEFAULT_SYSTEM_RAM_POOL_IN_GB = 16;
    constexpr const char *AMD_GPU_QUERY_CMD = "rocm-smi --showproductname --showmeminfo vram --csv 2>/dev/null";
    constexpr const char *AMD_QUERY_CARD_IDENTIFIER = "card";
    constexpr const char *LSPCI_GPU_QUERY_CMD = "lspci 2>/dev/null";
    constexpr const char *NVIDIA_GPU_QUERY_CMD = "nvidia-smi --query-gpu=index,name,memory.total --format=csv,noheader 2>/dev/null";
    constexpr const char *PROC_CPUINFO_PATH = "/proc/cpuinfo";
    constexpr const char *CGROUP_V1_PERIOD_PATH = "/sys/fs/cgroup/cpu/cpu.cfs_period_us";
    constexpr const char *CGROUP_V1_QUOTA_PATH = "/sys/fs/cgroup/cpu/cpu.cfs_quota_us";
    constexpr const char *CGROUP_V2_CPU_MAX_PATH = "/sys/fs/cgroup/cpu.max";
    constexpr const char *CGROUP_V2_UNLIMITED_QUOTA = "max";

    /**
     * Reads /proc/cpuinfo/ to find the corresponding number of cores on a CPU.
     * Matches pairs of physical core id and core id preceding it. For systems
     * that do not have this information, defaults to 1 logical core per physical
     * core.
     */
    void parse_cpuinfo(std::istream &input, uint32_t logical_cores, std::vector<Cpu> &cpus)
    {
        if (logical_cores == 0)
        {
            logical_cores = DEFAULT_CPU_CORES;
        }

        std::set<std::pair<int, int>> physical_cores;
        int current_physical_id = 0;
        std::string model_name = "Not available";

        std::string line;
        while (std::getline(input, line))
        {
            size_t colon = line.find(":");
            if (colon == std::string::npos || colon + 2 > line.length())
            {
                continue;
            }
            std::string value = line.substr(colon + 2);

            if (line.find("model name") == 0)
            {
                model_name = value;
            }
            else if (line.find("physical id") == 0)
            {
                try
                {
                    current_physical_id = std::stoi(value);
                }
                catch (const std::exception &)
                {
                }
            }
            else if (line.find("core id") == 0)
            {
                try
                {
                    physical_cores.insert(std::make_pair(current_physical_id, std::stoi(value)));
                }
                catch (const std::exception &)
                {
                }
            }
        }

        uint32_t physical_core_count = static_cast<uint32_t>(physical_cores.size());
        if (physical_core_count == 0)
        {
            physical_core_count = logical_cores;
        }

        cpus.emplace_back(Cpu(logical_cores, model_name, physical_core_count));
    }

    /**
     * Leverage the ROCm toolkit to get information about the GPU. Parse if data
     * is found, return nothing if not.
     */
    void parse_amd_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus)
    {
        uint8_t device_id = 0;

        for (const std::string &line : lines)
        {
            if (line.find(AMD_QUERY_CARD_IDENTIFIER) == 0)
            {
                size_t first_comma = line.find(',');
                size_t second_comma = line.find(',', first_comma + 1);
                if (first_comma != std::string::npos && second_comma != std::string::npos)
                {
                    try
                    {
                        std::string name = line.substr(first_comma + 1, second_comma - first_comma - 1);
                        std::string vram_str = line.substr(second_comma + 1);
                        uint64_t vram_bytes = std::stoull(vram_str);
                        gpus.emplace_back(Gpu(device_id++, name, vram_bytes, vram_bytes));
                    }
                    catch (const std::exception &e)
                    {
                        std::string what = static_cast<std::string>(e.what());
                        logger::WARN("AMD GPUs found, but data could not be parsed. Exception: " + what);
                    }
                }
            }
        }
    }

    /**
     * Leverage the lspci posix utility to get information about graphics controllers.
     * Parse if data is found, return nothing if not.
     */
    void parse_generic_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus)
    {
        uint8_t device_id = 0;
        for (const std::string &line : lines)
        {
            std::string lower_line = line;
            for (char &c : lower_line)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            if (lower_line.find("vga") != std::string::npos || lower_line.find("3d controller") != std::string::npos)
            {
                std::string name = "Generic Linux GPU";
                size_t colon = line.find(": ");
                if (colon != std::string::npos && colon + 2 < line.length())
                {
                    name = line.substr(colon + 2);
                    name.erase(name.find_last_not_of(" \n\r\t") + 1);
                }
                uint64_t total_memory_in_bytes = convert_gb_to_bytes(DEFAULT_GPU_VRAM_IN_GB);
                gpus.emplace_back(Gpu(device_id++, name, total_memory_in_bytes, total_memory_in_bytes));
            }
        }
    }

    /**
     * Leverage the nvidia-smi toolkit to get information about the GPU. Parse if data
     * is found; return nothing if not.
     */
    void parse_nvidia_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus)
    {
        for (const std::string &line : lines)
        {
            size_t first_comma = line.find(',');
            size_t second_comma = line.find(',', first_comma + 1);
            if (first_comma != std::string::npos && second_comma != std::string::npos)
            {
                try
                {
                    int id = std::stoi(line.substr(0, first_comma));
                    std::string name = line.substr(first_comma + 2, second_comma - first_comma - 2);
                    std::string vram_str = line.substr(second_comma + 2);
                    uint64_t vram_mb = std::stoull(vram_str);
                    uint64_t vram_bytes = vram_mb * BYTES_PER_MB;
                    gpus.emplace_back(Gpu(id, name, vram_bytes, vram_bytes));
                }
                catch (const std::exception &e)
                {
                    std::string what = static_cast<std::string>(e.what());
                    logger::WARN("NVIDIA GPUs found, but data could not be parsed. Exception: " + what);
                }
            }
        }
    }

    /**
     * Keeps the tightest of the candidate limits seen so far. A candidate of
     * zero means that source found no limit and is ignored.
     * @returns void
     */
    void consider_cpu_limit(uint32_t &limit, uint64_t candidate)
    {
        if (candidate > 0 && (limit == 0 || candidate < limit))
        {
            limit = static_cast<uint32_t>(candidate);
        }
    }

    /**
     * Reads the cgroup v2 quota, where "cpu.max" holds "<quota_us> <period_us>"
     * or "max <period_us>" when the group is unlimited.
     * @returns void
     */
    void parse_cgroup_v2_quota(std::istream &input, uint32_t &limit)
    {
        std::string quota_str;
        uint64_t period = 0;
        if ((input >> quota_str >> period) && quota_str != CGROUP_V2_UNLIMITED_QUOTA && period > 0)
        {
            try
            {
                uint64_t quota = std::stoull(quota_str);
                consider_cpu_limit(limit, (quota + period - 1) / period);
            }
            catch (const std::exception &)
            {
            }
        }
    }

    /**
     * Reads the cgroup v1 quota, where a quota of -1 means unlimited. Quotas
     * are rounded up to whole cores.
     * @returns void
     */
    void parse_cgroup_v1_quota(std::istream &quota_input, std::istream &period_input, uint32_t &limit)
    {
        long long quota = 0;
        long long period = 0;
        if ((quota_input >> quota) && (period_input >> period) && quota > 0 && period > 0)
        {
            consider_cpu_limit(limit, static_cast<uint64_t>((quota + period - 1) / period));
        }
    }

    // LCOV_EXCL_START

    /**
     * Reads the affinity mask of this process, which respects taskset pinning
     * and container cpusets.
     */
    void read_affinity_cpu_limit(uint32_t &limit)
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        if (sched_getaffinity(0, sizeof(mask), &mask) == 0)
        {
            int count = CPU_COUNT(&mask);
            if (count > 0)
            {
                consider_cpu_limit(limit, static_cast<uint64_t>(count));
            }
        }
    }

    std::vector<Cpu> get_cpus()
    {
        std::vector<Cpu> cpus;
        std::ifstream cpuinfo(PROC_CPUINFO_PATH);

        parse_cpuinfo(cpuinfo, std::thread::hardware_concurrency(), cpus);

        return cpus;
    }

    uint32_t get_effective_cpu_limit()
    {
        uint32_t limit = 0;

        read_affinity_cpu_limit(limit);

        std::ifstream cpu_max(CGROUP_V2_CPU_MAX_PATH);
        parse_cgroup_v2_quota(cpu_max, limit);

        std::ifstream quota_file(CGROUP_V1_QUOTA_PATH);
        std::ifstream period_file(CGROUP_V1_PERIOD_PATH);
        parse_cgroup_v1_quota(quota_file, period_file, limit);

        return limit;
    }

    /**
     * Walks the GPU query tiers in order, stopping at the first that reports a
     * device: the vendor tools give exact names and VRAM, lspci only an estimate.
     */
    std::vector<Gpu> get_gpus()
    {
        std::vector<Gpu> gpus;

        parse_nvidia_gpus(posix::get_command_stdout(NVIDIA_GPU_QUERY_CMD), gpus);
        if (!gpus.empty())
        {
            return gpus;
        }

        parse_amd_gpus(posix::get_command_stdout(AMD_GPU_QUERY_CMD), gpus);
        if (!gpus.empty())
        {
            return gpus;
        }

        parse_generic_gpus(posix::get_command_stdout(LSPCI_GPU_QUERY_CMD), gpus);
        return gpus;
    }

    OperatingSystem get_os()
    {
        return OperatingSystem("Linux");
    }

    Ram get_ram()
    {
        uint64_t total_ram = convert_gb_to_bytes(DEFAULT_SYSTEM_RAM_POOL_IN_GB);
        struct sysinfo mem_info;
        if (sysinfo(&mem_info) == 0)
        {
            total_ram = static_cast<uint64_t>(mem_info.totalram) * mem_info.mem_unit;
        }
        return Ram(total_ram);
    }

    // LCOV_EXCL_STOP
}
