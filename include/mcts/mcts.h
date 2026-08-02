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

#pragma once

#include "include/engine/engine.h"
#include "include/nn/nn.h"
#include <vector>
#include <memory>
#include <cmath>
#include <random>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <future>

namespace mcts
{
    const double PUCT_C_INIT = 1.25;
    const double PUCT_C_BASE = 19652.0;

    const double DIRICHLET_EPSILON = 0.25;
    const double DIRICHLET_ALPHA = 0.3;

    const int MAX_ROLLOUT_MOVES = 100;

    /* Hard ceiling on real OS threads the pool will ever attempt to create,
     * independent of the configured thread count. A misconfigured or
     * pathological request (e.g. thousands of threads) would otherwise
     * exhaust memory well before pthread_create ever cleanly fails --
     * each thread reserves a default ~8MB stack, so tens of thousands of
     * attempts can OOM the whole process before any exception is thrown.
     * No real configuration needs anywhere near this many search threads. */
    const size_t MAX_POOL_THREADS = 256;

    const double PIECE_VALUE_PAWN = 100.0;
    const double PIECE_VALUE_KNIGHT = 320.0;
    const double PIECE_VALUE_BISHOP = 330.0;
    const double PIECE_VALUE_ROOK = 500.0;
    const double PIECE_VALUE_QUEEN = 900.0;
    const double PIECE_VALUE_KING = 20000.0;

    class Node
    {
    public:
        Node(Node *parent_node, const chess::Move &m, uint8_t color);
        ~Node();

        Node *parent;
        std::vector<std::unique_ptr<Node>> children;
        chess::Move move;
        uint8_t color_to_move;

        std::atomic<int> visits;
        std::atomic<double> win_score;
        std::atomic<int> virtual_loss;

        double prior;
        std::atomic<bool> is_expanded;
        std::mutex expand_mutex;

        void add_dirichlet_noise(double epsilon, double alpha);
        void backpropagate(double result);
        void expand(chess::Engine &engine, const std::vector<double> &policy = {});
        int get_max_depth() const;
        double puct_value(int total_visits) const;
        Node *select_child();
    };

    /**
     * RAII helper that increments a node's virtual loss on construction and
     * decrements it on destruction, guaranteeing balance even if an
     * exception unwinds the stack mid-search.
     */
    struct VirtualLossGuard
    {
        Node *node;
        explicit VirtualLossGuard(Node *n) : node(n) { node->virtual_loss++; }
        ~VirtualLossGuard()
        {
            if (node)
                node->virtual_loss--;
        }

        /* Move-only: an implicit copy would not increment, so the pair of
         * destructors would decrement virtual_loss twice. */
        VirtualLossGuard(const VirtualLossGuard &) = delete;
        VirtualLossGuard &operator=(const VirtualLossGuard &) = delete;
        VirtualLossGuard(VirtualLossGuard &&other) noexcept : node(other.node) { other.node = nullptr; }
        VirtualLossGuard &operator=(VirtualLossGuard &&other) noexcept
        {
            if (this != &other)
            {
                if (node)
                    node->virtual_loss--;
                node = other.node;
                other.node = nullptr;
            }
            return *this;
        }
    };

    /**
     * One in-flight simulation slot in a pipelined search batch: either a
     * resolved terminal result, or a pending NN evaluation together with the
     * move path needed to cheaply re-reach its leaf for expansion.
     */
    struct PipelineItem
    {
        Node *node;
        std::vector<VirtualLossGuard> path_guards;
        std::future<nn::Result> eval_future;
        bool is_terminal;
        double terminal_result;
        std::vector<chess::Move> path_moves;
    };

    /**
     * Owns a persistent pool of num_threads worker threads and dispatches
     * search rounds to them, rather than spawning/joining fresh OS threads
     * on every call -- restarting the whole pool on every single move
     * (once per ply in self-play) turned out to account for a large share
     * of the gap between raw search throughput and real self-play
     * throughput. All public methods must be called sequentially from one
     * thread; the pool's dispatch state (current_round, round_engines,
     * generation) is shared, mutable, per-round data, not safe to drive
     * concurrently from two callers.
     */
    class Tree
    {
    public:
        Tree(nn::NN *nn, int threads, size_t pipeline_target);
        ~Tree();

        int benchmark_search(chess::Engine &engine, int time_limit_ms);
        chess::Move find_best_move(chess::Engine &engine, int time_limit_ms, int max_simulations);
        std::pair<chess::Move, std::vector<std::pair<chess::Move, double>>> find_best_move_with_policy(chess::Engine &engine, int simulations, bool apply_noise = false);

    private:
        /**
         * Builds a per-thread Engine copy of @p engine, carrying over its
         * move history so get_terminal_state() can detect repetitions that
         * involve positions from before the search root.
         */
        std::unique_ptr<chess::Engine> make_thread_engine(chess::Engine &engine);
        void search_worker(std::unique_ptr<chess::Engine> thread_engine, Node *root, int simulations, std::chrono::steady_clock::time_point end_time, bool use_time);
        double simulate(chess::Engine &engine);

        /**
         * One round's broadcast parameters: identical for every worker
         * dispatched into that round, published once under pool_mutex.
         */
        struct RoundParams
        {
            Node *root = nullptr;
            int simulations = 0;
            std::chrono::steady_clock::time_point end_time{};
            bool use_time = false;
        };

        /**
         * Body of each persistent pool thread: waits for either shutdown or
         * a new round (a generation bump), runs search_worker with that
         * round's parameters and this slot's engine clone, then signals
         * completion. Runs until shutdown_requested.
         */
        void pool_worker_loop(size_t index);

        /**
         * Runs one round of search across the persistent pool -- or inline,
         * on the calling thread, if the pool has zero live workers -- and
         * blocks until every dispatched worker has finished. Builds each
         * worker's Engine clone on the calling thread, in the same program
         * order the old per-call spawn loop did, since the clone must
         * reflect the board exactly as of this call.
         */
        void dispatch_round(chess::Engine &engine, Node *root, int simulations,
                             std::chrono::steady_clock::time_point end_time, bool use_time,
                             bool print_fallback_warning);

        nn::NN *evaluator;
        int num_threads;
        size_t pipeline_target;

        std::vector<std::thread> pool_threads;
        size_t pool_size = 0;

        std::mutex pool_mutex;
        std::condition_variable dispatch_cv;
        std::condition_variable done_cv;
        uint64_t generation = 0;
        bool shutdown_requested = false;
        size_t completed_in_round = 0;
        size_t active_workers_this_round = 0;
        RoundParams current_round;
        std::vector<std::unique_ptr<chess::Engine>> round_engines;
    };
}
