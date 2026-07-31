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

#include <gtest/gtest.h>
#include "include/logger/internal/logger.h"

using logger::compute_cap_count;
using logger::format_log_line;
using logger::LogBuffer;
using logger::should_drop_for_backpressure;

class LoggerInternalTest : public ::testing::Test
{
};

/* format_log_line */

TEST_F(LoggerInternalTest, FormatsEachRealLevel)
{
    EXPECT_EQ(format_log_line("msg", logger::LEVEL::CRITICAL, "TIME", "file.cpp", 10),
              "[TIME] [file.cpp @ Line 10]::[CRITICAL] - msg\n");
    EXPECT_EQ(format_log_line("msg", logger::LEVEL::DEBUG, "TIME", "file.cpp", 10),
              "[TIME] [file.cpp @ Line 10]::[DEBUG] - msg\n");
    EXPECT_EQ(format_log_line("msg", logger::LEVEL::ERROR, "TIME", "file.cpp", 10),
              "[TIME] [file.cpp @ Line 10]::[ERROR] - msg\n");
    EXPECT_EQ(format_log_line("msg", logger::LEVEL::INFO, "TIME", "file.cpp", 10),
              "[TIME] [file.cpp @ Line 10]::[INFO] - msg\n");
    EXPECT_EQ(format_log_line("msg", logger::LEVEL::WARN, "TIME", "file.cpp", 10),
              "[TIME] [file.cpp @ Line 10]::[WARN] - msg\n");
}

TEST_F(LoggerInternalTest, OutOfRangeLevelFormatsAsUnknownRatherThanThrowing)
{
    logger::LEVEL bogus = static_cast<logger::LEVEL>(255);
    EXPECT_EQ(format_log_line("msg", bogus, "TIME", "file.cpp", 1),
              "[TIME] [file.cpp @ Line 1]::[UNKNOWN] - msg\n");
}

TEST_F(LoggerInternalTest, EmptyMessageStillFormatsTheSurroundingLine)
{
    EXPECT_EQ(format_log_line("", logger::LEVEL::INFO, "TIME", "file.cpp", 1),
              "[TIME] [file.cpp @ Line 1]::[INFO] - \n");
}

TEST_F(LoggerInternalTest, LongMessagePassesThroughUnmodified)
{
    std::string long_message(10000, 'A');
    std::string formatted = format_log_line(long_message, logger::LEVEL::INFO, "TIME", "file.cpp", 1);
    EXPECT_NE(formatted.find(long_message), std::string::npos);
}

TEST_F(LoggerInternalTest, SpecialCharactersInMessagePassThroughUnmodified)
{
    std::string special = "Special chars: !@#$%^&*()_+{}|:<>?[]\\;'\",./ \n\t";
    std::string formatted = format_log_line(special, logger::LEVEL::INFO, "TIME", "file.cpp", 1);
    EXPECT_NE(formatted.find(special), std::string::npos);
}

TEST_F(LoggerInternalTest, KeepsEntriesBelowCap)
{
    EXPECT_FALSE(should_drop_for_backpressure(0, 10));
    EXPECT_FALSE(should_drop_for_backpressure(9, 10));
}

TEST_F(LoggerInternalTest, DropsEntriesAtOrAboveCap)
{
    EXPECT_TRUE(should_drop_for_backpressure(10, 10));
    EXPECT_TRUE(should_drop_for_backpressure(11, 10));
}

TEST_F(LoggerInternalTest, ZeroCapAlwaysDrops)
{
    EXPECT_TRUE(should_drop_for_backpressure(0, 0));
}

TEST_F(LoggerInternalTest, RawFractionUsedWhenWithinClampRange)
{
    uint64_t two_gb = 2ULL * 1024 * 1024 * 1024;
    size_t expected = static_cast<size_t>((two_gb / 100) / 256);
    EXPECT_EQ(compute_cap_count(two_gb, 0.01, 16ULL * 1024 * 1024, 256ULL * 1024 * 1024, 256), expected);
}

TEST_F(LoggerInternalTest, TinyMachineClampsToFloor)
{
    uint64_t five_hundred_twelve_mb = 512ULL * 1024 * 1024;
    size_t expected = static_cast<size_t>((16ULL * 1024 * 1024) / 256);
    EXPECT_EQ(compute_cap_count(five_hundred_twelve_mb, 0.01, 16ULL * 1024 * 1024, 256ULL * 1024 * 1024, 256), expected);
}

TEST_F(LoggerInternalTest, HugeMachineClampsToCeiling)
{
    uint64_t one_hundred_twenty_eight_gb = 128ULL * 1024 * 1024 * 1024;
    size_t expected = static_cast<size_t>((256ULL * 1024 * 1024) / 256);
    EXPECT_EQ(compute_cap_count(one_hundred_twenty_eight_gb, 0.01, 16ULL * 1024 * 1024, 256ULL * 1024 * 1024, 256), expected);
}

TEST_F(LoggerInternalTest, ZeroAssumedLineSizeReturnsZeroRatherThanDividingByZero)
{
    EXPECT_EQ(compute_cap_count(2ULL * 1024 * 1024 * 1024, 0.01, 16ULL * 1024 * 1024, 256ULL * 1024 * 1024, 0), 0u);
}

TEST_F(LoggerInternalTest, PushIncreasesSizeUntilCap)
{
    LogBuffer buffer(2);
    EXPECT_EQ(buffer.size(), 0u);

    buffer.push("a");
    EXPECT_EQ(buffer.size(), 1u);

    buffer.push("b");
    EXPECT_EQ(buffer.size(), 2u);
}

TEST_F(LoggerInternalTest, PushBeyondCapDropsAndCounts)
{
    LogBuffer buffer(1);
    buffer.push("a");
    buffer.push("b");
    buffer.push("c");

    EXPECT_EQ(buffer.size(), 1u);
    EXPECT_EQ(buffer.dropped_count(), 2u);
}

TEST_F(LoggerInternalTest, SwapOutReturnsBufferedLinesAndEmptiesIt)
{
    LogBuffer buffer(4);
    buffer.push("a");
    buffer.push("b");

    std::vector<std::string> drained = buffer.swap_out();

    ASSERT_EQ(drained.size(), 2u);
    EXPECT_EQ(drained.at(0), "a");
    EXPECT_EQ(drained.at(1), "b");
    EXPECT_EQ(buffer.size(), 0u);
}

TEST_F(LoggerInternalTest, BufferAcceptsAgainAfterSwapOut)
{
    LogBuffer buffer(1);
    buffer.push("a");
    buffer.swap_out();

    buffer.push("b");
    EXPECT_EQ(buffer.size(), 1u);
    EXPECT_EQ(buffer.dropped_count(), 0u);
}
