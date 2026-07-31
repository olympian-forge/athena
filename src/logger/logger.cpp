/*
 *   Copyright (c) 2025 Ike
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
#include "include/logger/internal/logger.h"
#include "include/hardware/hardware.h"
#include "include/chrono/chrono.h"
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace logger
{
    constexpr double MEMORY_FRACTION = 0.01;
    constexpr uint64_t MEMORY_FLOOR_BYTES = 16ULL * 1024 * 1024;
    constexpr uint64_t MEMORY_CEILING_BYTES = 256ULL * 1024 * 1024;
    constexpr size_t ASSUMED_AVERAGE_LINE_BYTES = 256;
    constexpr std::chrono::milliseconds WRITER_WAKE_TIMEOUT(200);

    const char *level_to_string(LEVEL level)
    {
        switch (level)
        {
        case LEVEL::CRITICAL:
            return "[CRITICAL] - ";
        case LEVEL::DEBUG:
            return "[DEBUG] - ";
        case LEVEL::ERROR:
            return "[ERROR] - ";
        case LEVEL::INFO:
            return "[INFO] - ";
        case LEVEL::WARN:
            return "[WARN] - ";
        }
        return "[UNKNOWN] - ";
    }

    std::string format_log_line(const std::string &message, LEVEL level,
                                const std::string &timestamp, const char *file, uint32_t line_number)
    {
        return "[" + timestamp + "] [" + file + " @ Line " + std::to_string(line_number) + "]::" +
               level_to_string(level) + message + "\n";
    }

    bool should_drop_for_backpressure(size_t current_count, size_t cap_count)
    {
        return current_count >= cap_count;
    }

    size_t compute_cap_count(uint64_t effective_memory_limit_bytes, double fraction,
                             uint64_t floor_bytes, uint64_t ceiling_bytes,
                             size_t assumed_average_line_bytes)
    {
        if (assumed_average_line_bytes == 0)
        {
            return 0;
        }

        uint64_t raw_bytes = static_cast<uint64_t>(static_cast<double>(effective_memory_limit_bytes) * fraction);
        uint64_t clamped_bytes = raw_bytes;
        if (clamped_bytes < floor_bytes)
        {
            clamped_bytes = floor_bytes;
        }
        if (clamped_bytes > ceiling_bytes)
        {
            clamped_bytes = ceiling_bytes;
        }

        return static_cast<size_t>(clamped_bytes / assumed_average_line_bytes);
    }

    LogBuffer::LogBuffer(size_t cap_count) : cap_count(cap_count), dropped(0)
    {
        lines.reserve(cap_count);
    }

    void LogBuffer::push(std::string line)
    {
        if (should_drop_for_backpressure(lines.size(), cap_count))
        {
            dropped++;
            return;
        }
        lines.push_back(std::move(line));
    }

    std::vector<std::string> LogBuffer::swap_out()
    {
        std::vector<std::string> drained;
        drained.reserve(cap_count);
        std::swap(drained, lines);
        return drained;
    }

    size_t LogBuffer::size() const { return lines.size(); }

    uint64_t LogBuffer::dropped_count() const { return dropped; }

    void rotate_logs()
    {
        namespace fs = std::filesystem;

        try
        {
            if (fs::exists(LOG_FILE) && fs::file_size(LOG_FILE) > MAX_LOG_SIZE)
            {
                std::string backup_file = std::string(LOG_FILE) + ".1";
                if (fs::exists(backup_file))
                {
                    fs::remove(backup_file);
                }
                fs::rename(LOG_FILE, backup_file);
            }
        }
        catch (...)
        {
        }
    }

    void write_batch(const std::vector<std::string> &batch)
    {
        if (batch.empty())
        {
            return;
        }

        rotate_logs();

        std::ofstream log_file(LOG_FILE, std::ios_base::app);
        if (!log_file)
        {
            return;
        }

        for (const std::string &line : batch)
        {
            log_file << line;
        }
    }

    struct LoggerData
    {
        std::mutex mutex;
        std::condition_variable cv;
        LogBuffer buffer;
        std::thread writer_thread;
        bool stop_requested;
        bool shut_down;

        LoggerData() : buffer(compute_cap_count(hardware::get_effective_memory_limit(), MEMORY_FRACTION,
                                                MEMORY_FLOOR_BYTES, MEMORY_CEILING_BYTES, ASSUMED_AVERAGE_LINE_BYTES)),
                       stop_requested(false), shut_down(false)
        {
        }
    };

    // LCOV_EXCL_START
    void writer_loop(LoggerData &impl)
    {
        while (true)
        {
            std::vector<std::string> batch;
            {
                std::unique_lock<std::mutex> lock(impl.mutex);
                impl.cv.wait_for(lock, WRITER_WAKE_TIMEOUT, [&impl]
                                 { return impl.stop_requested || impl.buffer.size() > 0; });
                if (impl.stop_requested && impl.buffer.size() == 0)
                {
                    break;
                }
                batch = impl.buffer.swap_out();
            }
            write_batch(batch);
        }
    }
    // LCOV_EXCL_STOP

    Logger::Logger() : impl(std::make_unique<LoggerData>())
    {
        impl->writer_thread = std::thread(writer_loop, std::ref(*impl));
    }

    Logger::~Logger()
    {
        shutdown();
    }

    Logger &Logger::get_instance()
    {
        static Logger instance;
        return instance;
    }

    void Logger::log(const std::string &message, LEVEL level, std::source_location location) const
    {
        if (level == LEVEL::DEBUG && !chess::DEBUG)
        {
            return;
        }

        std::string timestamp = chrono::Chrono().get_time_with_format("%a %b %d, %Y @ %H:%M:%S");
        std::string line = format_log_line(message, level, timestamp, location.file_name(),
                                           static_cast<uint32_t>(location.line()));

        {
            std::lock_guard<std::mutex> guard(impl->mutex);
            impl->buffer.push(std::move(line));
        }
        impl->cv.notify_one();
    }

    void Logger::flush() const
    {
        std::vector<std::string> batch;
        {
            std::lock_guard<std::mutex> guard(impl->mutex);
            batch = impl->buffer.swap_out();
        }
        write_batch(batch);
    }

    void Logger::shutdown() const
    {
        if (impl->shut_down)
        {
            return;
        }
        impl->shut_down = true;

        {
            std::lock_guard<std::mutex> guard(impl->mutex);
            impl->stop_requested = true;
        }
        impl->cv.notify_one();

        if (impl->writer_thread.joinable())
        {
            impl->writer_thread.join();
        }

        flush();
    }
}
