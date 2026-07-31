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
#include <istream>
#include <string>
#include <vector>
#include "include/hardware/hardware.h"

namespace hardware::platform
{

    void parse_amd_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus);

    void parse_generic_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus);

    void parse_nvidia_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus);

    void parse_cpuinfo(std::istream &input, uint32_t logical_cores, std::vector<Cpu> &cpus);

    void parse_cgroup_v2_quota(std::istream &input, uint32_t &limit);

    void parse_cgroup_v1_quota(std::istream &quota_input, std::istream &period_input, uint32_t &limit);

    void consider_cpu_limit(uint32_t &limit, uint64_t candidate);

    void parse_cgroup_v2_memory_limit(std::istream &input, uint64_t &limit);

    void parse_cgroup_v1_memory_limit(std::istream &input, uint64_t &limit);

    void consider_memory_limit(uint64_t &limit, uint64_t candidate);
}
