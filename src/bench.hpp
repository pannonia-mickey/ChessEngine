#ifndef CHESS_BENCH_HPP
#define CHESS_BENCH_HPP

namespace chess {

// Fixed-position, fixed-depth NPS benchmark. Searches a bundled, deterministic
// set of FENs one after another (each with a freshly-cleared transposition
// table, so no position's search can be influenced by another's), sums the
// total nodes searched and total wall-clock time, and prints a Stockfish-style
// summary ("Nodes searched", "Time", "Nodes/second") to stdout.
//
// The node count this prints for a given `depth` is meant to be a stable
// regression anchor: a change that only affects raw speed (compiler flags,
// data-structure/allocation tweaks) must leave it bit-for-bit identical while
// changing only the elapsed time/NPS. A change to search *decisions*
// (pruning, move ordering, extensions) will change the node count itself -
// that's expected, and is a signal the change belongs to a different
// (algorithmic) NPS bucket, not this one.
//
// `depth` is the fixed search depth applied to every bench position.
void run_bench(int depth);

// Default depth used when the "bench" UCI command is given no explicit depth
// argument.
constexpr int BENCH_DEFAULT_DEPTH = 12;

} // namespace chess

#endif // CHESS_BENCH_HPP
