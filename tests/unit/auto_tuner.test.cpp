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
#include <cstdio>
#include "include/tuner/auto_tuner.h"
#include "include/tuner/tuning_parameters.h"

class AutoTunerTest : public ::testing::Test
{
protected:
    void SetUp() override { std::remove("athena.cfg"); }

    void TearDown() override { std::remove("athena.cfg"); }
};

/* ------------------------------------------------------- construction --- */

TEST_F(AutoTunerTest, ConstructsFromTuningParameters)
{
    tuner::TuningParameters params(false);
    tuner::AutoTuner tuner_instance(params);

    SUCCEED();
}

TEST_F(AutoTunerTest, ConstructsWithUsageCaps)
{
    tuner::TuningParameters params(false);
    tuner::AutoTuner tuner_instance(params, 80.0, 50.0);

    SUCCEED();
}
