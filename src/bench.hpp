#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace chess {

// A fixed, deterministic workload whose node count fingerprints a build: two builds that report
// the same "bench" node count walk the same tree. Its nodes-per-second figure is a rough speed
// measure for comparing builds on the same machine.
//
// Until search exists the workload is a perft of every bench position; once search lands it
// becomes a fixed-depth search of the same positions, and the default depth is retuned.

inline constexpr int kDefaultBenchDepth = 5;

inline constexpr std::array<std::string_view, 12> kBenchFens = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
    "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
    "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "8/3k4/8/8/8/8/3PK3/8 w - - 0 1",
};

struct BenchResult {
    std::uint64_t nodes = 0;
    std::chrono::milliseconds elapsed{0};

    [[nodiscard]] std::uint64_t nps() const;
};

// Runs the bench workload, printing per-position counts and a summary to `out`. The last line has
// the form "<nodes> nodes <nps> nps", which is what OpenBench-style tooling parses.
BenchResult run_bench(std::ostream& out, int depth = kDefaultBenchDepth);

}  // namespace chess
