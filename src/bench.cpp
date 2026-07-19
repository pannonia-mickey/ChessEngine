#include "bench.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "position.hpp"
#include "search.hpp"
#include "tt.hpp"

namespace chess {

namespace {

// A fixed, deterministic set of positions spanning opening, middlegame,
// endgame, and tactical themes - the standard mix for an engine "bench"
// command. Order and contents must stay stable across commits: this list
// (together with `depth`) is what makes run_bench()'s node count a
// reproducible regression anchor.
const std::vector<std::string>& bench_fens() {
    static const std::vector<std::string> fens = {
        // Startpos
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        // Kiwipete (classic move-generator/search stress position)
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        // Open middlegame, both sides developed
        "r1bqkb1r/pp3ppp/2n1pn2/2pp4/3P4/2N1PN2/PPP2PPP/R1BQKB1R w KQkq - 0 6",
        // Ruy Lopez middlegame
        "r1bq1rk1/2p1bppp/p1np1n2/1p2p3/4P3/1BP2N2/PP1P1PPP/RNBQR1K1 w - - 0 9",
        // Sharp tactical middlegame (queens on, open center)
        "r2q1rk1/pp2bppp/2n1bn2/3pp3/3PP3/2N1BN2/PPP1BPPP/R2Q1RK1 w - - 0 9",
        // Endgame: rook + pawns
        "8/5pk1/6p1/8/7P/6P1/5PK1/8 w - - 0 1",
        // Endgame: opposite-colored bishops
        "8/3b4/8/5k2/8/3K4/4B3/8 w - - 0 1",
        // Endgame: queen vs rook
        "8/8/8/4k3/8/3K4/8/3R1Q2 w - - 0 1",
        // Passed-pawn race
        "6k1/1p3ppp/8/8/8/8/P4PPP/6K1 w - - 0 1",
        // Tactical: pinned/skewered pieces, mate threats
        "r1b1kb1r/pppp1ppp/2n2q2/4p3/2B1P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 4 5",
        // King safety / attack position, castled kings both sides
        "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 6 7",
        // Late endgame: king and pawns only, near-zugzwang
        "8/8/4k3/8/3K4/8/4P3/8 w - - 0 1",
    };
    return fens;
}

} // namespace

void run_bench(int depth) {
    std::uint64_t total_nodes = 0;
    auto start = std::chrono::steady_clock::now();

    int index = 0;
    for (const std::string& fen : bench_fens()) {
        ++index;
        Position pos;
        pos.set(fen);

        // Fresh table per position: keeps each search's node count
        // independent of bench-list order or prior positions' TT contents,
        // so the total is reproducible regardless of what ran before it.
        TranspositionTable tt(16);

        SearchLimits limits;
        limits.depth = depth;

        SearchResult result = search_best_move(pos, limits, tt);
        total_nodes += result.nodes;

        std::cout << "info string bench position " << index << '/'
                   << bench_fens().size() << " nodes " << result.nodes
                   << std::endl;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::uint64_t nps = elapsed_ms > 0
        ? total_nodes * 1000ULL / static_cast<std::uint64_t>(elapsed_ms)
        : total_nodes;

    std::cout << "===========================" << std::endl;
    std::cout << "Total time (ms) : " << elapsed_ms << std::endl;
    std::cout << "Nodes searched  : " << total_nodes << std::endl;
    std::cout << "Nodes/second    : " << nps << std::endl;
}

} // namespace chess
