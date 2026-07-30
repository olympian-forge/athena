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
 * Internals of the Linux backend. Deliberately NOT in include/hardware/ --
 * nothing outside src/hardware/ and its tests may depend on these; the public
 * contract stays the five functions in include/hardware/platform.h.
 *
 * Everything here takes data (already-read lines, an open stream) rather than
 * a command or a path, so the parsing is exercised by handing it strings. The
 * code that actually reaches the outside world -- popen, /proc, sysinfo -- is
 * kept to a thin layer in linux.cpp and excluded from coverage instead.
 */

#pragma once
#include <istream>
#include <string>
#include <vector>
#include "include/hardware/hardware.h"

namespace hardware::platform
{
    /**
     * Parses rocm-smi --csv output. Rows describing a device start with the
     * card identifier and carry name and VRAM after the first two commas.
     */
    void parse_amd_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus);

    /**
     * Parses lspci output, matching VGA and 3D controller entries. The model
     * name is taken from the original line so adapter casing is preserved.
     */
    void parse_generic_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus);

    /**
     * Parses nvidia-smi --format=csv,noheader output: "<index>, <name>, <MiB>".
     */
    void parse_nvidia_gpus(const std::vector<std::string> &lines, std::vector<Gpu> &gpus);

    /**
     * Parses /proc/cpuinfo content. A physical core is one distinct
     * (physical id, core id) pair; kernels that publish neither fall back to
     * one physical core per logical core.
     */
    void parse_cpuinfo(std::istream &input, uint32_t logical_cores, std::vector<Cpu> &cpus);

    /**
     * Parses a cgroup v2 "cpu.max" stream, holding "<quota_us> <period_us>"
     * or "max <period_us>" when unlimited. Quotas round up to whole cores.
     */
    void parse_cgroup_v2_quota(std::istream &input, uint32_t &limit);

    /**
     * Parses the cgroup v1 quota/period pair. A quota of -1 means unlimited.
     */
    void parse_cgroup_v1_quota(std::istream &quota_input, std::istream &period_input, uint32_t &limit);

    /**
     * Keeps the tightest of the candidate limits seen so far. A candidate of
     * zero means that source found no limit and is ignored.
     */
    void consider_cpu_limit(uint32_t &limit, uint64_t candidate);
}
