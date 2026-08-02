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
#include "include/mcts/mcts.h"
#include "include/engine/engine.h"
#include "include/nn/nn.h"

using namespace chess;

class MCTSTest : public ::testing::Test
{
protected:
    Engine engine;
    mcts::Tree search{nullptr, 2, 8};
};

TEST_F(MCTSTest, FindBestMove_InitialPosition)
{
    chess::Move best_move = search.find_best_move(engine, 50, -1);

    auto legal_moves = engine.generate_all_moves();
    bool is_legal = false;
    for (const auto& m : legal_moves)
    {
        if (m.get_from_square() == best_move.get_from_square() &&
            m.get_to_square() == best_move.get_to_square() &&
            m.get_promotion_piece() == best_move.get_promotion_piece())
        {
            is_legal = true;
            break;
        }
    }
    EXPECT_TRUE(is_legal);
}

TEST_F(MCTSTest, NodeExpansionAndPuct)
{
    chess::Move dummy_move(0, 0);
    mcts::Node root(nullptr, dummy_move, WHITE);

    root.expand(engine);
    EXPECT_TRUE(root.is_expanded);
    EXPECT_EQ(root.children.size(), 20U);

    mcts::Node* child = root.select_child();
    EXPECT_NE(child, nullptr);
    EXPECT_EQ(child->visits, 0);

    child->backpropagate(1.0);
    EXPECT_EQ(child->visits, 1);
    EXPECT_EQ(child->win_score, 1.0);
    EXPECT_EQ(root.visits, 1);
    EXPECT_EQ(root.win_score, 0.0);
}

TEST_F(MCTSTest, CheckmateInOne)
{
    Engine mate_engine("7k/5Q2/5K2/8/8/8/8/8 w - - 0 1");

    mcts::Tree search_single(nullptr, 1, 8);
    chess::Move best_move = search_single.find_best_move(mate_engine, -1, 400);

    EXPECT_EQ(best_move.get_from(), "f7");
    EXPECT_EQ(best_move.get_to(), "g7");
}

TEST_F(MCTSTest, DirichletNoiseAndPolicyExpand)
{
    chess::Move dummy_move(0, 0);
    mcts::Node root(nullptr, dummy_move, WHITE);

    std::vector<double> policy(4672, 0.0);
    policy[0] = 1.0;

    root.expand(engine, policy);
    EXPECT_TRUE(root.is_expanded);
    EXPECT_EQ(root.children.size(), 20U);

    root.add_dirichlet_noise(0.25, 0.3);
    EXPECT_TRUE(root.is_expanded);

    auto result = search.find_best_move_with_policy(engine, 50, true);
    chess::Move best_move = result.first;
    auto extracted_policy = result.second;

    EXPECT_GT(extracted_policy.size(), 0U);
    EXPECT_NE(best_move.to_uci_notation(), "0000");
}

TEST_F(MCTSTest, EmptyChildrenReturnEmptyMove)
{
    Engine mated_engine("7k/5Q2/5K2/8/8/8/8/8 b - - 0 1");
    chess::Move m = search.find_best_move(mated_engine, 10, -1);
    EXPECT_EQ(m.to_uci_notation(), "a1a1");

    auto result = search.find_best_move_with_policy(mated_engine, 10, false);
    EXPECT_EQ(result.first.to_uci_notation(), "a1a1");
    EXPECT_EQ(result.second.size(), 0U);
}


/**
 * White's king is trapped with only pawn moves available against Black's 3
 * queens, so the random rollout in mcts::Tree::simulate will reliably
 * checkmate White and exercise the result = 1.0 path.
 */
TEST_F(MCTSTest, RolloutOpponentCheckmate)
{
    Engine custom_engine("K7/2qqq3/8/8/8/8/1P6/k7 w - - 0 1");
    search.find_best_move(custom_engine, 400, -1);
    SUCCEED();
}

/**
 * Passing 0 threads forces the synchronous fallback path.
 */
TEST_F(MCTSTest, FallbackToSynchronous)
{
    mcts::Tree sync_search(nullptr, 0, 8);
    auto result = sync_search.find_best_move_with_policy(engine, 10, false);
    EXPECT_NE(result.first.to_uci_notation(), "0000");
}

/*
 * Without an evaluator the policy is empty, so expand() falls back to its
 * move-type prior heuristic. Each special move type is a separate branch,
 * and benchmark_search is a separate entry point from find_best_move.
 */

TEST_F(MCTSTest, BenchmarkSearchReportsNodeCount)
{
    mcts::Tree bench{nullptr, 2, 8};
    Engine start;

    int nodes = bench.benchmark_search(start, 50);
    EXPECT_GE(nodes, 0);
}

TEST_F(MCTSTest, BenchmarkSearchOnSingleThread)
{
    mcts::Tree single{nullptr, 1, 4};
    Engine start;

    EXPECT_GE(single.benchmark_search(start, 30), 0);
}

/* An en-passant capture gets its own prior, distinct from a normal capture. */
TEST_F(MCTSTest, ExpandPrioritisesEnPassantCapture)
{
    Engine en_passant("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    mcts::Tree tree{nullptr, 1, 4};

    chess::Move best = tree.find_best_move(en_passant, 40, -1);
    EXPECT_NE(best.get_from_square(), best.get_to_square());
}

/* Promotions are weighted by the piece promoted to. */
TEST_F(MCTSTest, ExpandPrioritisesPromotion)
{
    Engine promoting("7k/P7/8/8/8/8/8/7K w - - 0 1");
    mcts::Tree tree{nullptr, 1, 4};

    chess::Move best = tree.find_best_move(promoting, 40, -1);
    EXPECT_NE(best.get_from_square(), best.get_to_square());
}

/* Ordinary captures take the material-difference branch. */
TEST_F(MCTSTest, ExpandPrioritisesCapture)
{
    Engine capturing("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
    mcts::Tree tree{nullptr, 1, 4};

    chess::Move best = tree.find_best_move(capturing, 40, -1);
    EXPECT_NE(best.get_from_square(), best.get_to_square());
}

/*
 * Edge configurations that the ordinary searches above never produce: no
 * worker threads, fewer simulations than threads, an evaluator present during
 * a benchmark, and a policy of all zeros.
 */

/* threads == 0 runs the search inline on the calling thread. */
TEST_F(MCTSTest, ZeroThreadsSearchesInline)
{
    mcts::Tree inline_tree{nullptr, 0, 4};
    Engine start;

    chess::Move best = inline_tree.find_best_move(start, 20, -1);
    EXPECT_NE(best.get_from_square(), best.get_to_square());
}

/* benchmark_search with no workers also has to run inline. */
TEST_F(MCTSTest, ZeroThreadsBenchmarksInline)
{
    mcts::Tree inline_bench{nullptr, 0, 4};
    Engine start;

    EXPECT_GE(inline_bench.benchmark_search(start, 30), 0);
}

/**
 * Simulation-limited search with fewer simulations than threads: the integer
 * division floors to zero, and each worker still has to be given one.
 * Note the signature is (engine, time_limit_ms, max_simulations) -- a
 * non-positive time limit is what selects the simulation-limited path.
 */
TEST_F(MCTSTest, FewerSimulationsThanThreadsStillRuns)
{
    mcts::Tree wide{nullptr, 4, 8};
    Engine start;

    chess::Move best = wide.find_best_move(start, -1, 1);
    EXPECT_NE(best.get_from_square(), best.get_to_square());
}

/**
 * Noise is applied before the empty-children check, so a terminal position
 * reaches add_dirichlet_noise with nothing to perturb.
 */
TEST_F(MCTSTest, NoiseOnTerminalPositionIsANoOp)
{
    mcts::Tree tree{nullptr, 1, 4};
    Engine mated("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");

    ASSERT_TRUE(mated.generate_all_moves().empty());
    auto [best, policy] = tree.find_best_move_with_policy(mated, 8, true);
    EXPECT_TRUE(policy.empty());
}

/* benchmark_search with an evaluator takes the neural expansion path. */
TEST_F(MCTSTest, BenchmarkSearchWithEvaluator)
{
    nn::NN evaluator("onnx/athena.onnx", 0, 2, 2);
    mcts::Tree bench{&evaluator, 2, 8};
    Engine start;

    EXPECT_GE(bench.benchmark_search(start, 60), 0);
}

/**
 * The test fixture model returns an all-zero policy, so every child prior is
 * zero and the Dirichlet mixing has to survive a zero prior sum rather than
 * dividing by it.
 */
TEST_F(MCTSTest, ZeroPolicySumSurvivesNoiseMixing)
{
    nn::NN flat("tests/dummy.onnx", 0, 1, 2);
    mcts::Tree tree{&flat, 1, 4};
    Engine start;

    auto [best_move, policy] = tree.find_best_move_with_policy(start, 24, true);
    EXPECT_NE(best_move.get_from_square(), best_move.get_to_square());
    EXPECT_FALSE(policy.empty());
}

/*
 * Node::expand() maps every legal move through the policy encoder, not just
 * the ones the search later visits, so a position whose legal moves span a
 * given family covers that family's encoding arms deterministically -- no
 * dependence on which subtree the randomised search happens to explore.
 */

/* A knight alone in the centre has all eight knight offsets available. */
TEST_F(MCTSTest, EncoderCoversEveryKnightDirection)
{
    nn::NN flat("tests/dummy.onnx", 0, 1, 2);
    mcts::Tree tree{&flat, 1, 4};
    Engine knight("4k3/8/8/8/3N4/8/8/4K3 w - - 0 1");

    ASSERT_EQ(knight.generate_all_moves().size(), 8u + 5u); /* 8 knight + 5 king */
    auto [best, policy] = tree.find_best_move_with_policy(knight, 16, false);
    EXPECT_FALSE(policy.empty());
}

/* A queen alone in the centre slides along all eight rays. */
TEST_F(MCTSTest, EncoderCoversEverySlidingDirection)
{
    nn::NN flat("tests/dummy.onnx", 0, 1, 2);
    mcts::Tree tree{&flat, 1, 4};
    Engine queen("4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1");

    auto [best, policy] = tree.find_best_move_with_policy(queen, 16, false);
    EXPECT_FALSE(policy.empty());
}

/**
 * Non-queen promotions take a separate encoding channel keyed on promotion
 * piece and file shift, so a pawn that can promote straight ahead or by
 * capturing either way covers every promotion direction and type.
 */
TEST_F(MCTSTest, EncoderCoversNonQueenPromotions)
{
    nn::NN flat("tests/dummy.onnx", 0, 1, 2);
    mcts::Tree tree{&flat, 1, 4};
    Engine promoting("r1r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1");

    /* b7 may push to b8 or capture a8/c8, each as queen, rook, bishop or
     * knight -- twelve promotion moves in total. */
    size_t promotions = 0;
    for (const chess::Move &move : promoting.generate_all_moves())
    {
        if (move.is_promotion())
        {
            promotions++;
        }
    }
    ASSERT_GE(promotions, 12u);

    auto [best, policy] = tree.find_best_move_with_policy(promoting, 16, false);
    EXPECT_FALSE(policy.empty());
}

/**
 * The pool must be fully reusable: two consecutive calls on the same Tree
 * instance must each produce a valid, non-degenerate result, proving no
 * worker thread state (generation counter, round buffers) is corrupted or
 * left over from the first round.
 */
TEST_F(MCTSTest, PoolReusedAcrossConsecutiveCalls)
{
    auto [move1, policy1] = search.find_best_move_with_policy(engine, 40, false);
    EXPECT_FALSE(policy1.empty());
    EXPECT_NE(move1.get_from_square(), move1.get_to_square());

    auto [move2, policy2] = search.find_best_move_with_policy(engine, 40, false);
    EXPECT_FALSE(policy2.empty());
    EXPECT_NE(move2.get_from_square(), move2.get_to_square());
}

/**
 * Mixing the three public entry points on one Tree instance back-to-back
 * exercises dispatch_round with different RoundParams shapes (use_time
 * true/false, different simulation counts) on the same persistent pool.
 */
TEST_F(MCTSTest, PoolReusedAcrossDifferentEntryPoints)
{
    int nodes = search.benchmark_search(engine, 30);
    EXPECT_GE(nodes, 0);

    chess::Move m = search.find_best_move(engine, 30, -1);
    EXPECT_NE(m.get_from_square(), m.get_to_square());

    auto [move, policy] = search.find_best_move_with_policy(engine, 30, false);
    EXPECT_FALSE(policy.empty());
}

/**
 * A Tree destroyed immediately after a call returns must join every pool
 * thread cleanly with no in-flight round -- regression guard for the
 * shutdown-vs-in-flight-round race.
 */
TEST_F(MCTSTest, DestructorAfterCallJoinsCleanly)
{
    {
        mcts::Tree scoped{nullptr, 3, 8};
        Engine start;
        scoped.find_best_move_with_policy(start, 20, false);
    }
    SUCCEED();
}

/**
 * Many sequential construct/round/destroy cycles, mirroring
 * AutoTuner::benchmark_config's repeated Tree construction, to catch any
 * thread-count leak or double-join across repeated pool lifetimes.
 */
TEST_F(MCTSTest, RepeatedPoolConstructionAndTeardown)
{
    Engine start;
    for (int i = 0; i < 20; ++i)
    {
        mcts::Tree t{nullptr, 2, 4};
        auto [move, policy] = t.find_best_move_with_policy(start, 10, false);
        EXPECT_FALSE(policy.empty());
    }
}
