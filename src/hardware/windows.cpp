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
/* NOMINMAX before windows.h: without it windows.h defines max/min as macros,
 * which mangles any later std::max/std::min in this translation unit. */
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <thread>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "advapi32.lib")

namespace hardware::platform
{
    constexpr uint32_t CPU_RATE_SCALE = 10000;
    constexpr uint32_t DEFAULT_CPU_CORES = 4;
    constexpr uint8_t DEFAULT_SYSTEM_RAM_POOL_IN_GB = 16;
    constexpr size_t CPU_MODEL_NAME_BUFFER_SIZE = 256;
    constexpr const char *BASIC_RENDER_DRIVER_IDENTIFIER = "Basic Render Driver";
    constexpr const char *CPU_REGISTRY_KEY = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    constexpr const char *CPU_REGISTRY_VALUE = "ProcessorNameString";

    /**
     * Reads the processor registry key for the model name and walks the
     * RelationProcessorCore records to find the number of cores on a CPU.
     * Each record describes one physical core. For systems that do not
     * report this information, defaults to 1 logical core per physical core.
     */
    void find_and_extract_cpus(std::vector<Cpu> &cpus)
    {
        uint32_t logical_cores = std::thread::hardware_concurrency();
        if (logical_cores == 0)
        {
            logical_cores = DEFAULT_CPU_CORES;
        }

        std::string model_name = "Not available";
        char model_name_buffer[CPU_MODEL_NAME_BUFFER_SIZE];
        DWORD model_name_size = static_cast<DWORD>(sizeof(model_name_buffer));
        if (RegGetValueA(HKEY_LOCAL_MACHINE, CPU_REGISTRY_KEY, CPU_REGISTRY_VALUE, RRF_RT_REG_SZ, nullptr, model_name_buffer, &model_name_size) == ERROR_SUCCESS)
        {
            model_name = model_name_buffer;
        }

        uint32_t physical_core_count = 0;
        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
        if (length > 0)
        {
            std::vector<uint8_t> buffer(length);
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length))
            {
                DWORD offset = 0;
                while (offset < length)
                {
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
                    if (record->Size == 0)
                    {
                        break;
                    }
                    physical_core_count += 1;
                    offset += record->Size;
                }
            }
        }

        if (physical_core_count == 0)
        {
            physical_core_count = logical_cores;
        }

        cpus.emplace_back(Cpu(logical_cores, model_name, physical_core_count));
    }

    /**
     * Leverage DXGI to get information about the graphics adapters. DXGI is
     * vendor agnostic, so NVIDIA, AMD and Intel adapters all enumerate here.
     * Parse if data is found, return nothing if not.
     */
    void find_and_extract_dxgi_gpus(std::vector<Gpu> &gpus)
    {
        IDXGIFactory *factory = nullptr;
        if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory))))
        {
            logger::WARN("DXGI factory could not be created, so no GPUs could be detected.");
            return;
        }

        IDXGIAdapter *adapter = nullptr;
        UINT adapter_index = 0;
        uint8_t device_id = 0;
        while (factory->EnumAdapters(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND)
        {
            if (adapter == nullptr)
            {
                break;
            }

            DXGI_ADAPTER_DESC description;
            if (SUCCEEDED(adapter->GetDesc(&description)))
            {
                std::string name;
                for (size_t i = 0; i < ARRAYSIZE(description.Description) && description.Description[i] != L'\0'; ++i)
                {
                    name.push_back(static_cast<char>(description.Description[i]));
                }

                if (name.find(BASIC_RENDER_DRIVER_IDENTIFIER) == std::string::npos)
                {
                    uint64_t vram_bytes = static_cast<uint64_t>(description.DedicatedVideoMemory);
                    gpus.emplace_back(Gpu(device_id++, name, vram_bytes, vram_bytes));
                }
            }
            adapter->Release();
            adapter_index++;
        }
        factory->Release();
    }

    /**
     * Keeps the tightest of the candidate limits seen so far. A candidate of
     * zero means that source found no limit and is ignored.
     */
    void consider_cpu_limit(uint32_t &limit, uint64_t candidate)
    {
        if (candidate > 0 && (limit == 0 || candidate < limit))
        {
            limit = static_cast<uint32_t>(candidate);
        }
    }

    /**
     * Reads the affinity mask of this process, which respects SetProcessAffinityMask
     * and the CPUs a container is confined to. The mask only describes the
     * processor group this process is assigned to, so on machines with more
     * than 64 logical processors it covers that group alone.
     */
    void find_affinity_cpu_limit(uint32_t &limit)
    {
        DWORD_PTR process_mask = 0;
        DWORD_PTR system_mask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
        {
            uint32_t count = 0;
            for (size_t bit = 0; bit < sizeof(process_mask) * 8; ++bit)
            {
                if ((process_mask >> bit) & 1)
                {
                    count += 1;
                }
            }
            consider_cpu_limit(limit, static_cast<uint64_t>(count));
        }
    }

    /**
     * Reads the CPU rate cap of the job object this process belongs to, which
     * is what Windows containers throttle with. CpuRate is expressed in
     * hundredths of a percent of total machine capacity, and is rounded up to
     * whole cores.
     */
    void find_job_object_cpu_limit(uint32_t &limit)
    {
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate_control;
        DWORD returned_length = 0;
        if (!QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &rate_control, sizeof(rate_control), &returned_length))
        {
            return;
        }

        if ((rate_control.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) == 0 || rate_control.CpuRate == 0)
        {
            return;
        }

        uint32_t logical_cores = std::thread::hardware_concurrency();
        if (logical_cores == 0)
        {
            logical_cores = DEFAULT_CPU_CORES;
        }

        uint64_t allowed = static_cast<uint64_t>(rate_control.CpuRate) * logical_cores;
        consider_cpu_limit(limit, (allowed + CPU_RATE_SCALE - 1) / CPU_RATE_SCALE);
    }

    std::vector<Cpu> get_cpus()
    {
        std::vector<Cpu> cpus;

        find_and_extract_cpus(cpus);

        return cpus;
    }

    uint32_t get_effective_cpu_limit()
    {
        uint32_t limit = 0;

        find_affinity_cpu_limit(limit);
        find_job_object_cpu_limit(limit);

        return limit;
    }

    std::vector<Gpu> get_gpus()
    {
        std::vector<Gpu> gpus;

        find_and_extract_dxgi_gpus(gpus);

        return gpus;
    }

    OperatingSystem get_os()
    {
        return OperatingSystem("Windows");
    }

    Ram get_ram()
    {
        uint64_t total_ram = convert_gb_to_bytes(DEFAULT_SYSTEM_RAM_POOL_IN_GB);
        MEMORYSTATUSEX memory_status;
        memory_status.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memory_status))
        {
            total_ram = static_cast<uint64_t>(memory_status.ullTotalPhys);
        }
        return Ram(total_ram);
    }
}
