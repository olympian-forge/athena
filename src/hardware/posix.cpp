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

#include "include/hardware/posix.h"
#include <stdio.h>

namespace hardware::platform::posix
{
    /* Pure I/O boundary: spawns a command and collects its stdout. Kept
     * small enough to verify by reading; excluded from coverage because the
     * only ways to exercise its failure paths are fork/descriptor exhaustion
     * or a production seam, and neither is worth having. */

    // LCOV_EXCL_START
    std::vector<std::string> get_command_stdout(const char *command)
    {
        std::vector<std::string> output;

        FILE *pipe = popen(command, "r");
        if (!pipe)
        {
            return output;
        }

        char buffer[POPEN_BUFFER_SIZE];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            output.emplace_back(buffer);
        }

        pclose(pipe);
        return output;
    }
    // LCOV_EXCL_STOP
}
