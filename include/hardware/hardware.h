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

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hardware
{

    constexpr uint64_t BYTES_PER_GB = 1024ULL * 1024ULL * 1024ULL;
    constexpr uint64_t BYTES_PER_MB = 1024ULL * 1024ULL;

    enum class CpuType
    {
        AMD,
        INTEL
    };

    enum class GpuType
    {
        AMD,
        NVIDIA
    };

    enum class OperatingSystemType
    {
        LINUX,
        MACOS,
        WINDOWS
    };

    class Cpu
    {
        CpuType cpu_type;
        uint32_t logical_cores;
        std::string model_name;
        uint32_t physical_cores;

        void set_cpu_type(const std::string &model_name);

    public:
        Cpu(uint32_t logical_cores, const std::string &model_name, uint32_t physical_cores);

        ~Cpu();

        CpuType get_cpu_type() const;

        uint32_t get_logical_cores() const;

        std::string get_model_name() const;

        uint32_t get_physical_cores() const;
    };

    class Gpu
    {
        int device_id;
        GpuType gpu_type;
        bool is_amd_gpu;
        bool is_nvidia_gpu;
        std::string model_name;
        uint64_t total_memory_in_bytes;
        uint64_t vram_size_in_bytes;

        void set_gpu_type(const std::string &model_name);

    public:
        Gpu(int device_id, const std::string &model_name, uint64_t total_memory_in_bytes, uint64_t vram_size_in_bytes);

        ~Gpu();

        int get_device_id() const;

        GpuType get_gpu_type() const;

        bool get_is_amd_gpu() const;

        bool get_is_nvidia_gpu() const;

        std::string get_model_name() const;

        uint64_t get_total_memory_in_bytes() const;

        uint64_t get_vram_size_in_bytes() const;
    };

    class Ram
    {
        uint64_t total_size_in_bytes;

    public:
        Ram(uint64_t total_size_in_bytes);

        ~Ram();

        uint64_t get_total_size_in_bytes() const;
    };

    class OperatingSystem
    {
        bool is_64_bit;
        bool is_windows;
        std::string name;
        OperatingSystemType os_type;

    public:
        OperatingSystem(const std::string &name);

        ~OperatingSystem();

        bool get_is_64_bit() const;

        bool get_is_windows() const;

        std::string get_name() const;

        OperatingSystemType get_os_type() const;
    };

    class HostInfo
    {
        std::vector<Cpu> cpus;
        std::vector<Gpu> gpus;
        OperatingSystem os;
        Ram ram;

    public:
        HostInfo(const std::vector<Cpu> &cpus, const std::vector<Gpu> &gpus, const Ram &ram, const OperatingSystem &os);

        HostInfo();

        ~HostInfo();

        std::vector<Cpu> get_cpus() const;

        std::vector<Gpu> get_gpus() const;

        OperatingSystem get_os() const;

        Ram get_ram() const;
    };

    uint32_t convert_bytes_to_gb(uint64_t bytes);

    uint64_t convert_gb_to_bytes(uint32_t gb);

    uint32_t get_effective_cpu_limit();

    HostInfo detect_host_info();
}
