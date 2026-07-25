/*
 *   Copyright (c) 2026 Ike

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https:
 */

#include <gtest/gtest.h>
#include "include/abi/c_api.h"
#include <cstring>
#include <string>

class CApiTest : public ::testing::Test {
protected:
    void* engine = nullptr;

    void SetUp() override {
        engine = athena_create();
    }

    void TearDown() override {
        if (engine) {
            athena_destroy(engine);
        }
    }
};

TEST_F(CApiTest, MakeMoveValidAndInvalid) {
    EXPECT_TRUE(athena_make_move(engine, "e2", "e4"));
    /* Invalid move - it's black's turn now */
    EXPECT_FALSE(athena_make_move(engine, "d2", "d4"));
    /* Invalid move format or pseudo-illegal */
    EXPECT_FALSE(athena_make_move(engine, "e8", "e9"));
}

TEST_F(CApiTest, GenerateAllMoves) {
    char buffer[1024];
    int count = athena_generate_all_moves(engine, buffer, sizeof(buffer));
    EXPECT_EQ(count, 20); /* 20 initial moves for white */
    EXPECT_TRUE(strstr(buffer, "e2e4") != nullptr);
}

TEST_F(CApiTest, GetFen) {
    char buffer[256];
    athena_get_fen(engine, buffer, sizeof(buffer));
    EXPECT_STREQ(buffer, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST_F(CApiTest, PrintBoard) {
    testing::internal::CaptureStdout();
    athena_print_board(engine);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(output.empty());
}
