#!/bin/bash

#   Copyright (c) 2026 Ike
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

# -----------------------------------------------------------------------------
# Athena Chess Engine Coverage Table Generator
# -----------------------------------------------------------------------------
# Generates a per-file coverage table (covered lines, uncovered line numbers,
# percentage) by reading the lcov .info file's own SF:/DA: records directly,
# rather than parsing `lcov --list`'s human-readable ASCII table.
#
# The previous version scraped that rendered table with awk/grep, which broke
# on CI: a different Ubuntu lcov package build (2.0-4ubuntu2 vs this machine's
# 2.0-1) formatted it just differently enough that field extraction picked up
# the wrong columns, producing nonsense percentages ("Unable to detect lines"
# for files lcov's own summary reported at 100%). The .info format itself
# (SF:/DA:/end_of_record) is lcov's stable machine-readable output -- documented,
# not subject to column-width or locale rendering -- so reading it directly is
# correct regardless of which lcov build produced it.
#
# This also fixes a second, independent bug: the old version cross-referenced
# raw `gcov` .gcov files for uncovered line numbers, and plain gcov has no
# concept of LCOV_EXCL_LINE/START/STOP -- a line excluded from coverage.info
# still shows "#####:" (zero executions) in the .gcov file. Every excluded line
# in this codebase (the hardware/NVML/popen I/O boundaries, the mcts/nn/selfplay
# defensive guards) was therefore listed as "uncovered" even though it correctly
# doesn't count against the gate. Reading DA: records directly is authoritative:
# lcov's own capture step already removed excluded lines from coverage.info, so
# what's left is exactly what the gate enforces.
# -----------------------------------------------------------------------------

set -e

COVERAGE_INFO="$1"

if [ -z "$COVERAGE_INFO" ]; then
    echo "Usage: $0 <coverage_info_file>"
    exit 1
fi

echo "📊 Per-File Coverage Table:"
echo "┌─────────────────────────────────────────────────┬─────────┬─────────────────────────────────────┬─────────┐"
echo "│ File                                            │ Covered │ Uncovered Lines                     │ Percent │"
echo "├─────────────────────────────────────────────────┼─────────┼─────────────────────────────────────┼─────────┤"

awk '
    # SF: is an absolute path (e.g. /workspaces/athena/src/hardware/hardware.cpp
    # locally, /home/runner/work/athena/athena/src/... on CI). Display relative
    # to the last "src/" segment, matching the convention used everywhere else
    # in this build (e.g. "hardware/hardware.cpp"), regardless of checkout path.
    /^SF:/ {
        path = substr($0, 4)
        src_pos = match(path, /\/src\//)
        if (src_pos > 0) {
            file = substr(path, src_pos + length("/src/"))
        } else {
            file = path
        }
        total = 0
        covered = 0
        uncovered = ""
        next
    }

    /^DA:/ {
        split(substr($0, 4), parts, ",")
        line_num = parts[1]
        hits = parts[2]
        total++
        if (hits + 0 > 0) {
            covered++
        } else {
            uncovered = (uncovered == "") ? line_num : uncovered ", " line_num
        }
        next
    }

    /^end_of_record/ {
        if (total > 0) {
            if (covered == total) {
                percent = "100%"
                display = "None (100% covered!)"
            } else {
                percent = sprintf("%.1f%%", (covered * 100.0) / total)
                display = uncovered
            }
            printf "│ %-47s │ %7d │ %-35s │ %7s │\n", file, covered, display, percent
        }
    }
' "$COVERAGE_INFO"

echo "└─────────────────────────────────────────────────┴─────────┴─────────────────────────────────────┴─────────┘"
