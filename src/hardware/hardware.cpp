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

#include "include/hardware/hardware.h"
#include "include/hardware/platform.h"
#include <iostream>
#include <algorithm>

#if !defined(_WIN32) && !defined(__linux__)
#error "Athena supports Windows and Linux only. There is no hardware backend for this platform (see src/hardware/ -- each supported platform needs its own implementation of include/hardware/platform.h)."
#endif

namespace hardware
{
    Cpu::Cpu(uint32_t logical_cores, const std::string &model_name, uint32_t physical_cores)
        : logical_cores(logical_cores), model_name(model_name), physical_cores(physical_cores)
    {
        set_cpu_type(model_name);
    }

    Cpu::~Cpu() = default;

    CpuType Cpu::get_cpu_type() const { return cpu_type; }

    uint32_t Cpu::get_logical_cores() const { return logical_cores; }

    std::string Cpu::get_model_name() const { return model_name; }

    uint32_t Cpu::get_physical_cores() const { return physical_cores; }

    void Cpu::set_cpu_type(const std::string &model)
    {
        std::string lower_model = model;
        std::transform(lower_model.begin(), lower_model.end(), lower_model.begin(), [](unsigned char c)
                       { return static_cast<char>(::tolower(c)); });

        if (lower_model.find("amd") != std::string::npos || lower_model.find("ryzen") != std::string::npos || lower_model.find("epyc") != std::string::npos)
        {
            cpu_type = CpuType::AMD;
            return;
        }
        cpu_type = CpuType::INTEL;
    }

    Gpu::Gpu(int device_id, const std::string &model_name, uint64_t total_memory_in_bytes, uint64_t vram_size_in_bytes)
        : device_id(device_id), gpu_type(GpuType::AMD), is_amd_gpu(false), is_nvidia_gpu(false), model_name(model_name), total_memory_in_bytes(total_memory_in_bytes), vram_size_in_bytes(vram_size_in_bytes)
    {
        set_gpu_type(model_name);
    }

    Gpu::~Gpu() = default;

    int Gpu::get_device_id() const { return device_id; }

    GpuType Gpu::get_gpu_type() const { return gpu_type; }

    bool Gpu::get_is_amd_gpu() const { return is_amd_gpu; }

    bool Gpu::get_is_nvidia_gpu() const { return is_nvidia_gpu; }

    std::string Gpu::get_model_name() const { return model_name; }

    uint64_t Gpu::get_total_memory_in_bytes() const { return total_memory_in_bytes; }

    uint64_t Gpu::get_vram_size_in_bytes() const { return vram_size_in_bytes; }

    void Gpu::set_gpu_type(const std::string &model)
    {
        std::string lower_model = model;
        std::transform(lower_model.begin(), lower_model.end(), lower_model.begin(), [](unsigned char c)
                       { return static_cast<char>(::tolower(c)); });

        is_amd_gpu = false;
        is_nvidia_gpu = false;

        if (lower_model.find("amd") != std::string::npos || lower_model.find("radeon") != std::string::npos)
        {
            gpu_type = GpuType::AMD;
            is_amd_gpu = true;
            return;
        }
        gpu_type = GpuType::NVIDIA;
        is_nvidia_gpu = true;
    }

    Ram::Ram(uint64_t total_size_in_bytes) : total_size_in_bytes(total_size_in_bytes) {}

    Ram::~Ram() = default;

    uint64_t Ram::get_total_size_in_bytes() const { return total_size_in_bytes; }

    OperatingSystem::OperatingSystem(const std::string &name)
        : is_64_bit(sizeof(void *) == 8), is_windows(false), name(name), os_type(OperatingSystemType::LINUX)
    {
#ifdef _WIN32
        os_type = OperatingSystemType::WINDOWS;
        is_windows = true;
#else
        os_type = OperatingSystemType::LINUX;
#endif
    }

    OperatingSystem::~OperatingSystem() = default;

    bool OperatingSystem::get_is_64_bit() const { return sizeof(void *) == 8; }

    bool OperatingSystem::get_is_windows() const { return os_type == OperatingSystemType::WINDOWS; }

    std::string OperatingSystem::get_name() const { return name; }

    OperatingSystemType OperatingSystem::get_os_type() const { return os_type; }

    HostInfo::HostInfo(const std::vector<Cpu> &cpus, const std::vector<Gpu> &gpus, const Ram &ram, const OperatingSystem &os)
        : cpus(cpus), gpus(gpus), os(os), ram(ram) {}

    HostInfo::HostInfo() : os("Unknown"), ram(0) {}

    HostInfo::~HostInfo() = default;

    std::vector<Cpu> HostInfo::get_cpus() const { return cpus; }

    std::vector<Gpu> HostInfo::get_gpus() const { return gpus; }

    OperatingSystem HostInfo::get_os() const { return os; }

    Ram HostInfo::get_ram() const { return ram; }

    uint32_t convert_bytes_to_gb(uint64_t bytes) { return static_cast<uint32_t>(bytes / BYTES_PER_GB) + 1; }

    uint64_t convert_gb_to_bytes(uint32_t gb) { return static_cast<uint64_t>(gb) * BYTES_PER_GB; }

    uint32_t get_effective_cpu_limit()
    {
        return platform::get_effective_cpu_limit();
    }

    HostInfo detect_host_info()
    {
        return HostInfo(platform::get_cpus(), platform::get_gpus(), platform::get_ram(), platform::get_os());
    }
}

#ifdef HARDWARE_STANDALONE_TEST
int main()
{
    hardware::HostInfo info = hardware::detect_host_info();

    std::cout << "--- HARDWARE INFO ---\n";

    std::cout << "[OS]\n";
    std::cout << "  Name: " << info.get_os().get_name() << "\n";
    std::cout << "  64-bit: " << (info.get_os().get_is_64_bit() ? "Yes" : "No") << "\n\n";

    std::cout << "[RAM]\n";
    std::cout << "  Total Memory: " << hardware::convert_bytes_to_gb(info.get_ram().get_total_size_in_bytes()) << " GB\n\n";

    std::cout << "[CPU]\n";
    for (const auto &cpu : info.get_cpus())
    {
        std::cout << "  Model: " << cpu.get_model_name() << "\n";
        std::cout << "  Logical Cores: " << cpu.get_logical_cores() << "\n";
        std::cout << "  Vendor: " << (cpu.get_cpu_type() == hardware::CpuType::AMD ? "AMD" : "Intel") << "\n";
    }
    std::cout << "\n";

    std::cout << "[GPU]\n";
    if (info.get_gpus().empty())
    {
        std::cout << "  No GPUs detected natively in this basic stub. (Requires NVML/DXGI)\n";
    }
    else
    {
        for (const auto &gpu : info.get_gpus())
        {
            std::cout << "  Device ID: " << gpu.get_device_id() << "\n";
            std::cout << "  Model: " << gpu.get_model_name() << "\n";
            std::cout << "  VRAM (GB): " << hardware::convert_bytes_to_gb(gpu.get_vram_size_in_bytes()) << "\n\n";
        }
    }

    return 0;
}
#endif
