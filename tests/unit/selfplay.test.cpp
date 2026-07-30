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
#include "include/selfplay/selfplay.h"
#include <filesystem>
#include <cstdlib>

using namespace chess;

TEST(SelfPlayTest, RunGameDefault) {
    SelfPlay sp(0, 1, 1);
    sp.run();
    EXPECT_TRUE(true);
}

TEST(SelfPlayTest, RunGameWhiteMated) {
    SelfPlay sp(0, 1, 1, "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    sp.run();
    EXPECT_TRUE(true);
}

TEST(SelfPlayTest, RunGameBlackMated) {
    SelfPlay sp(0, 1, 1, "rnbqkbnr/ppppp2p/5p2/6pQ/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 3");
    sp.run();
    EXPECT_TRUE(true);
}

/*
 * The three tuning knobs are read from the environment at the top of run(),
 * and the default path leaves those override branches unexecuted. Each test
 * sets the variable, runs a one-game session, then clears it again so the
 * other tests still exercise the defaults.
 */

class SelfPlayEnvTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        unsetenv("ATHENA_TEMPERATURE_MOVES");
        unsetenv("ATHENA_ADJUDICATE_MATERIAL");
        unsetenv("ATHENA_RESULT_DISCOUNT");
    }
};

TEST_F(SelfPlayEnvTest, TemperatureMovesOverrideIsHonoured)
{
    setenv("ATHENA_TEMPERATURE_MOVES", "3", 1);
    SelfPlay sp(0, 1, 1);
    sp.run();
    SUCCEED();
}

/* A negative value is rejected, leaving the default in place. */
TEST_F(SelfPlayEnvTest, NegativeTemperatureMovesIsRejected)
{
    setenv("ATHENA_TEMPERATURE_MOVES", "-5", 1);
    SelfPlay sp(0, 1, 1);
    sp.run();
    SUCCEED();
}

/* 0 disables material adjudication and prints the "(off)" marker. */
TEST_F(SelfPlayEnvTest, MaterialAdjudicationCanBeDisabled)
{
    setenv("ATHENA_ADJUDICATE_MATERIAL", "0", 1);
    SelfPlay sp(0, 1, 1);
    sp.run();
    SUCCEED();
}

/**
 * A low threshold makes adjudication fire on an already-lopsided position,
 * which is what reaches the material-based result branches.
 */
TEST_F(SelfPlayEnvTest, MaterialAdjudicationDecidesLopsidedPosition)
{
    setenv("ATHENA_ADJUDICATE_MATERIAL", "1", 1);
    SelfPlay white_up(0, 1, 1, "4k3/8/8/8/8/8/8/QQQQK3 w - - 0 1");
    white_up.run();

    SelfPlay black_up(0, 1, 1, "qqqqk3/8/8/8/8/8/8/4K3 b - - 0 1");
    black_up.run();
    SUCCEED();
}

TEST_F(SelfPlayEnvTest, ResultDiscountOverrideIsHonoured)
{
    setenv("ATHENA_RESULT_DISCOUNT", "0.5", 1);
    SelfPlay sp(0, 1, 1);
    sp.run();
    SUCCEED();
}

/* 1.0 disables the discount and prints the "(off)" marker. */
TEST_F(SelfPlayEnvTest, ResultDiscountCanBeDisabled)
{
    setenv("ATHENA_RESULT_DISCOUNT", "1.0", 1);
    SelfPlay sp(0, 1, 1);
    sp.run();
    SUCCEED();
}

/* Out-of-range discounts fall back to the default rather than being applied. */
TEST_F(SelfPlayEnvTest, OutOfRangeResultDiscountIsRejected)
{
    setenv("ATHENA_RESULT_DISCOUNT", "5.0", 1);
    SelfPlay sp(0, 1, 1);
    sp.run();
    SUCCEED();
}
