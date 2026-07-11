#ifndef CHESS_SEARCH_HPP
#define CHESS_SEARCH_HPP

#include <atomic>
#include <cstdint>
#include "move.hpp"

namespace chess {

class Position;

struct SearchLimits {
    int depth = 6;
    long long movetime_ms = 0;
    // When set, polled periodically during the search; setting it to true
    // (e.g. from a UCI "stop" command handled on another thread) aborts the
    // search early, same as a movetime deadline expiring.
    std::atomic<bool>* stop = nullptr;
};

struct SearchResult {
    Move best = MOVE_NONE;
    int score = 0;
    std::uint64_t nodes = 0;
    int depth = 0;
};

// Mate score: a checkmate found `ply` plies from the root scores as
// -MATE + ply (from the perspective of the side that is mated), so
// faster mates score more extreme than slower ones.
constexpr int MATE = 30000;

// Upper bound on iterative-deepening depth when time (not depth) governs
// the search. Kept small enough for the mate-scoring ply counter.
constexpr int MAX_DEPTH = 64;

// Iterative-deepening negamax with alpha-beta pruning, searching from
// depth 1 up to limits.depth (or until limits.movetime_ms elapses, if set).
SearchResult search_best_move(Position& pos, const SearchLimits& limits);

}

#endif // CHESS_SEARCH_HPP
