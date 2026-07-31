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
#include "include/core/core.h"

namespace logger
{

    std::string format_log_line(const std::string &message, LEVEL level,
                                const std::string &timestamp, const char *file, uint32_t line_number);

    bool should_drop_for_backpressure(size_t current_count, size_t cap_count);

    size_t compute_cap_count(uint64_t effective_memory_limit_bytes, double fraction,
                             uint64_t floor_bytes, uint64_t ceiling_bytes,
                             size_t assumed_average_line_bytes);

    class LogBuffer
    {
    private:
        std::vector<std::string> lines;
        size_t cap_count;
        uint64_t dropped;

    public:
        explicit LogBuffer(size_t cap_count);

        void push(std::string line);

        std::vector<std::string> swap_out();

        size_t size() const;

        uint64_t dropped_count() const;
    };
}
