/*
 *   Copyright (c) 2025 Ike

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <source_location>
#include <stdint.h>
#include <string>
#include "include/core/core.h"

namespace logger
{

    struct LoggerData;

    class Logger
    {
    private:
        Logger();

        ~Logger();

        std::unique_ptr<LoggerData> impl;

    public:
        Logger(const Logger &) = delete;
        Logger(Logger &&) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger &operator=(Logger &&) = delete;

        static Logger &get_instance();

        void log(const std::string &message, LEVEL level,
                 std::source_location location = std::source_location::current()) const;

        void flush() const;

        void shutdown() const;
    };

    inline void critical(const std::string &message,
                         std::source_location location = std::source_location::current())
    {
        Logger::get_instance().log(message, LEVEL::CRITICAL, location);
    }

    inline void debug(const std::string &message,
                      std::source_location location = std::source_location::current())
    {
        Logger::get_instance().log(message, LEVEL::DEBUG, location);
    }

    inline void error(const std::string &message,
                      std::source_location location = std::source_location::current())
    {
        Logger::get_instance().log(message, LEVEL::ERROR, location);
    }

    inline void info(const std::string &message,
                     std::source_location location = std::source_location::current())
    {
        Logger::get_instance().log(message, LEVEL::INFO, location);
    }

    inline void warn(const std::string &message,
                     std::source_location location = std::source_location::current())
    {
        Logger::get_instance().log(message, LEVEL::WARN, location);
    }
}
