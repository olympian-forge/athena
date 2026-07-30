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
#include "include/uci/uci.h"
#include <sstream>

using namespace chess;

TEST(UCITest, LoopCommands) {
    std::istringstream in("uci\nisready\nucinewgame\nposition startpos\nposition fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\nposition startpos moves e2e4\nposition startpos moves e2e4 e7e5\ngo nodes 10\nquit\n");
    std::ostringstream out;
    
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());
    
    UCI uci;
    uci.loop();
    
    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);
    
    std::string output = out.str();
    EXPECT_TRUE(output.find("uciok") != std::string::npos);
    EXPECT_TRUE(output.find("readyok") != std::string::npos);
    EXPECT_TRUE(output.find("bestmove") != std::string::npos);
}

TEST(UCITest, LoopCommandsUnknownGo) {
    std::istringstream in("go unknwn 10\nquit\n");
    std::ostringstream out;
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());
    
    UCI uci;
    uci.loop();
    
    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);
}

TEST(UCITest, LoopCommandsTimeManagement) {
    std::istringstream in("position startpos\ngo movetime 10\ngo wtime 30000 btime 30000\ngo wtime 100 btime 100\nquit\n");
    std::ostringstream out;
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());
    
    UCI uci;
    uci.loop();
    
    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);
    
    std::string output = out.str();
    EXPECT_TRUE(output.find("bestmove") != std::string::npos);
}


/**
 * winc/binc are parsed only when present; the earlier tests never send them,
 * so the increment branches stay unexercised without this.
 */
TEST(UCITest, GoParsesClockIncrements) {
    std::istringstream in("position startpos\ngo wtime 60000 btime 60000 winc 1000 binc 1000\nquit\n");
    std::ostringstream out;
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());

    UCI uci;
    uci.loop();

    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);

    EXPECT_TRUE(out.str().find("bestmove") != std::string::npos);
}

/**
 * An illegal move in a position command must abort the rest of the move list
 * rather than silently desyncing the board from the GUI.
 */
TEST(UCITest, PositionStopsAtIllegalMove) {
    std::istringstream in("position startpos moves e2e4 e7e5 a1a8 g1f3\ngo nodes 5\nquit\n");
    std::ostringstream out;
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());

    UCI uci;
    uci.loop();

    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);

    EXPECT_TRUE(out.str().find("illegal move") != std::string::npos);
}

/* The literal "moves" token is a separator and must be skipped, not applied. */
TEST(UCITest, PositionFenWithMovesToken) {
    std::istringstream in("position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves e2e4\ngo nodes 5\nquit\n");
    std::ostringstream out;
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());

    UCI uci;
    uci.loop();

    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);

    EXPECT_TRUE(out.str().find("bestmove") != std::string::npos);
}

/**
 * A repeated "moves" token is skipped rather than treated as a move. Malformed
 * GUI input is the only way to reach that guard, since the fen parser stops at
 * the first "moves" and startpos consumes it.
 */
TEST(UCITest, PositionSkipsRepeatedMovesToken) {
    std::istringstream in("position startpos moves e2e4 moves e7e5\ngo nodes 5\nquit\n");
    std::ostringstream out;
    std::streambuf* cinbuf = std::cin.rdbuf();
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());

    UCI uci;
    uci.loop();

    std::cin.rdbuf(cinbuf);
    std::cout.rdbuf(coutbuf);

    EXPECT_TRUE(out.str().find("bestmove") != std::string::npos);
    EXPECT_TRUE(out.str().find("illegal move") == std::string::npos);
}
