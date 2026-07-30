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

#include "include/tuner/tuning_parameters.h"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace tuner
{
    /**
     * @brief Constructs TuningParameters and optionally loads search parameters from configuration file.
     * @param load_from_file If true, the constructor attempts to parse athena.cfg for custom parameters.
     */
    TuningParameters::TuningParameters(bool load_from_file)
    {
        bool loaded = false;
        if (load_from_file)
        {
            std::ifstream cfg("athena.cfg");
            if (cfg.is_open())
            {
                std::string line;
                while (std::getline(cfg, line))
                {
                    if (line.find("SearchThreads=") == 0)
                        search_threads = static_cast<uint8_t>(std::stoi(line.substr(14)));
                    else if (line.find("BatchSize=") == 0)
                        batch_size = static_cast<uint16_t>(std::stoi(line.substr(10)));
                    else if (line.find("PipelineTarget=") == 0)
                        pipeline_target = static_cast<uint16_t>(std::stoi(line.substr(15)));
                    else if (line.find("BatchTimeoutMs=") == 0)
                        batch_timeout_ms = static_cast<uint16_t>(std::stoi(line.substr(15)));
                }

                if (search_threads > 0 && batch_size > 0 && pipeline_target > 0 && batch_timeout_ms > 0)
                {
                    loaded = true;
                    std::cout << "info string Loaded hardware parameters from athena.cfg\n";
                }
            }
        }

        if (!loaded)
        {
            host_info = hardware::detect_host_info();
            detected_cpu_limit = hardware::get_effective_cpu_limit();
            gpu_count = static_cast<uint8_t>(host_info->get_gpus().size());
            search_threads = calculate_search_threads();
            batch_size = calculate_batch_size();
            pipeline_target = calculate_pipeline_target();
            batch_timeout_ms = DEFAULT_BATCH_TIMEOUT_MS;
        }
        else
        {
            gpu_count = 1;
        }
    }

    /**
     * @brief Constructs TuningParameters from a supplied hardware profile
     * rather than detecting one, so sizing can be exercised against machine
     * shapes the running host does not have.
     * @param detected_host_info The hardware profile to size against.
     * @param effective_cpu_limit Cores this process may use; 0 means no limit.
     */
    TuningParameters::TuningParameters(const hardware::HostInfo &detected_host_info, uint32_t effective_cpu_limit)
        : host_info(detected_host_info), detected_cpu_limit(effective_cpu_limit)
    {
        gpu_count = static_cast<uint8_t>(detected_host_info.get_gpus().size());
        search_threads = calculate_search_threads();
        batch_size = calculate_batch_size();
        pipeline_target = calculate_pipeline_target();
        batch_timeout_ms = DEFAULT_BATCH_TIMEOUT_MS;
    }

    /**
     * @brief Destructs the TuningParameters object.
     */
    TuningParameters::~TuningParameters() = default;

    /**
     * @brief Computes optimal inference batch size based on available GPU count and VRAM.
     * @returns The calculated batch size.
     */
    uint16_t TuningParameters::calculate_batch_size()
    {
        uint64_t total_vram = 0;
        if (host_info)
        {
            for (const auto &gpu : host_info->get_gpus())
            {
                total_vram += gpu.get_vram_size_in_bytes();
            }
        }

        return decide_batch_size(gpu_count, total_vram, search_threads);
    }

    /**
     * @brief Pure batch-size decision, kept free of hardware lookups so every
     * VRAM tier is reachable from a test.
     * @param gpu_count Number of GPUs to size for; 0 selects the CPU default.
     * @param total_vram_in_bytes Summed VRAM across those GPUs.
     * @param search_threads Threads the search will run.
     * @returns The calculated batch size.
     */
    uint16_t TuningParameters::decide_batch_size(uint8_t gpu_count, uint64_t total_vram_in_bytes, uint8_t search_threads)
    {
        if (gpu_count == 0)
        {
            return DEFAULT_CPU_BATCH_SIZE;
        }

        uint64_t avg_vram = total_vram_in_bytes / gpu_count;
        uint32_t avg_vram_gb = hardware::convert_bytes_to_gb(avg_vram);

        uint32_t batch_per_gpu = 256;
        if (avg_vram_gb >= 16)
        {
            batch_per_gpu = 1024;
        }
        else if (avg_vram_gb >= 8)
        {
            batch_per_gpu = 512;
        }

        uint32_t gpu_batch = static_cast<uint32_t>(batch_per_gpu * gpu_count);
        uint32_t search_thread_batch = search_threads * DEFAULT_CPU_BATCH_SIZE;

        return static_cast<uint16_t>(std::min(gpu_batch, std::max(static_cast<uint32_t>(DEFAULT_CPU_BATCH_SIZE), search_thread_batch)));
    }

    /**
     * @brief Computes optimal pipeline size based on batch size and search threads.
     * @returns The calculated pipeline target.
     */
    uint16_t TuningParameters::calculate_pipeline_target()
    {
        uint32_t base_pipeline = batch_size * DEFAULT_BASE_PIPELINE;

        uint32_t thread_minimum = search_threads * DEFAULT_THREAD_MINIMUM_PIPELINE_BATCH;

        return static_cast<uint16_t>(std::max(base_pipeline, thread_minimum));
    }

    /**
     * @brief Computes optimal number of search threads based on logical CPU cores.
     * @returns The calculated number of threads.
     */
    uint8_t TuningParameters::calculate_search_threads()
    {
        uint8_t cores = DEFAULT_CPU_CORES;
        if (host_info && host_info->get_cpus().size() > 0)
        {
            cores = static_cast<uint8_t>(host_info->get_cpus().at(0).get_logical_cores());
        }

        return decide_search_threads(cores, detected_cpu_limit);
    }

    /**
     * @brief Pure search-thread decision. In containers /proc/cpuinfo shows the
     * host's cores but the cgroup quota / affinity mask is what the scheduler
     * grants, so the count is clamped to the effective allowance first.
     * @param cores Logical cores the machine reports.
     * @param effective_cpu_limit Cores this process may use; 0 means no limit.
     * @returns The calculated number of threads.
     */
    uint8_t TuningParameters::decide_search_threads(uint8_t cores, uint32_t effective_cpu_limit)
    {
        if (effective_cpu_limit > 0 && effective_cpu_limit < static_cast<uint32_t>(cores))
        {
            cores = static_cast<uint8_t>(effective_cpu_limit);
        }

        if (cores > 4)
        {
            return cores - 2;
        }
        else if (cores > 1)
        {
            return cores - 1;
        }
        return 1;
    }

    /**
     * @brief Translates a logical GPU index to its physical hardware Device ID.
     * @param index The 0-based logical index in the detected list.
     * @returns The underlying physical device identifier.
     */
    uint8_t TuningParameters::get_gpu_id(uint8_t index) const
    {
        if (host_info && index < host_info->get_gpus().size())
        {
            return static_cast<uint8_t>(host_info->get_gpus().at(index).get_device_id());
        }
        return 0;
    }
}
