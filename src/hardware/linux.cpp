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
#include "include/hardware/posix.h"
#include <cctype>
#include <fstream>
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

    /**
     * Reads /proc/cpuinfo/ to find the corresponding number of cores on a CPU.
     * Matches pairs of physical core id and core id preceding it. For systems
     * that do not have this information, defaults to 1 logical core per physical
     * core.
     * @returns void
     */
    void find_and_extract_cpus(std::vector<Cpu> &cpus)
    {
        uint32_t logical_cores = std::thread::hardware_concurrency();
        if (logical_cores == 0)
        {
            logical_cores = DEFAULT_CPU_CORES;
        }

        std::set<std::pair<int, int>> physical_cores;
        int current_physical_id = 0;
        std::string model_name = "Not available";

        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line))
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
    void find_and_extract_amd_gpus(std::vector<Gpu> &gpus)
    {
        std::vector<std::string> command_lines = posix::get_command_stdout(AMD_GPU_QUERY_CMD);
        uint8_t device_id = 0;

        for (const std::string &line : command_lines)
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
    void find_and_extract_generic_gpus(std::vector<Gpu> &gpus)
    {
        std::vector<std::string> command_lines = posix::get_command_stdout(LSPCI_GPU_QUERY_CMD);

        uint8_t device_id = 0;
        for (const std::string &line : command_lines)
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
    void find_and_extract_nvidia_gpus(std::vector<Gpu> &gpus)
    {
        std::vector<std::string> command_lines = posix::get_command_stdout(NVIDIA_GPU_QUERY_CMD);

        for (const std::string &line : command_lines)
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

    std::vector<Cpu> get_cpus()
    {
        std::vector<Cpu> cpus;

        find_and_extract_cpus(cpus);

        return cpus;
    }

    std::vector<Gpu> get_gpus()
    {
        std::vector<Gpu> gpus;

        find_and_extract_nvidia_gpus(gpus);

        if (!gpus.empty())
        {
            return gpus;
        }

        find_and_extract_amd_gpus(gpus);

        if (!gpus.empty())
        {
            return gpus;
        }

        find_and_extract_generic_gpus(gpus);

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
}
