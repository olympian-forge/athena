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
 * UtilizationMonitor loads NVML at runtime. On a machine without an NVIDIA
 * driver every load path fails by design and the monitor settles into its
 * "GPU unknown" state, which is exactly what these tests pin down -- the
 * failure handling is the part that has to be right on CPU-only hosts.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include "include/tuner/utilization.h"

class UtilizationMonitorTest : public ::testing::Test
{
protected:
    /* Burns a measurable slice of CPU so the process-time delta is non-zero
     * without depending on wall-clock scheduling. */
    static double burn_cpu()
    {
        double total = 0.0;
        for (int i = 1; i < 4000000; ++i)
        {
            total += std::sqrt(static_cast<double>(i));
        }
        return total;
    }
};

TEST_F(UtilizationMonitorTest, NonNvidiaMonitorNeverLoadsNvml)
{
    tuner::UtilizationMonitor monitor(0, false);

    EXPECT_FALSE(monitor.gpu_available());
    EXPECT_DOUBLE_EQ(monitor.cpu_percent(), -1.0);
    EXPECT_DOUBLE_EQ(monitor.gpu_percent(), -1.0);
}

/**
 * Asking for NVML on a host without the driver must degrade to the same
 * "unknown" state rather than failing construction.
 */
TEST_F(UtilizationMonitorTest, NvidiaMonitorSurvivesMissingDriver)
{
    tuner::UtilizationMonitor monitor(0, true);

    /* gpu_available() is true only where a real NVIDIA driver answered. */
    if (!monitor.gpu_available())
    {
        EXPECT_DOUBLE_EQ(monitor.gpu_percent(), -1.0);
    }
}

TEST_F(UtilizationMonitorTest, BeginEndProducesCpuPercentage)
{
    tuner::UtilizationMonitor monitor(0, false);

    monitor.begin();
    burn_cpu();
    monitor.end();

    EXPECT_GE(monitor.cpu_percent(), 0.0);
}

/**
 * Without a GPU handle there are no samples to average, so the GPU figure
 * stays at the unknown sentinel even across a full measurement window.
 */
TEST_F(UtilizationMonitorTest, GpuPercentStaysUnknownWithoutDevice)
{
    tuner::UtilizationMonitor monitor(0, false);

    monitor.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    monitor.end();

    if (!monitor.gpu_available())
    {
        EXPECT_DOUBLE_EQ(monitor.gpu_percent(), -1.0);
    }
}

/**
 * begin() clears the previous window, so a monitor can be reused without the
 * earlier measurement leaking into the next one.
 */
TEST_F(UtilizationMonitorTest, MonitorIsReusableAcrossWindows)
{
    tuner::UtilizationMonitor monitor(0, false);

    monitor.begin();
    burn_cpu();
    monitor.end();
    double first = monitor.cpu_percent();

    monitor.begin();
    EXPECT_DOUBLE_EQ(monitor.cpu_percent(), -1.0);
    burn_cpu();
    monitor.end();

    EXPECT_GE(monitor.cpu_percent(), 0.0);
    EXPECT_GE(first, 0.0);
}

/**
 * An out-of-range device index cannot resolve a handle, so even on a machine
 * that does have NVML the monitor reports the GPU as unknown.
 */
TEST_F(UtilizationMonitorTest, ImplausibleDeviceIndexYieldsNoHandle)
{
    tuner::UtilizationMonitor monitor(9999, true);
    EXPECT_FALSE(monitor.gpu_available());
}

/**
 * Destroying a monitor mid-window has to stop the sampler and unload NVML
 * rather than leaving the thread running.
 */
TEST_F(UtilizationMonitorTest, DestructorStopsAnActiveWindow)
{
    {
        tuner::UtilizationMonitor monitor(0, false);
        monitor.begin();
        burn_cpu();
    }

    SUCCEED();
}
