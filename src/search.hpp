#ifndef CHESS_SEARCH_HPP
#define CHESS_SEARCH_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>
#include "move.hpp"
#include "zobrist.hpp"

namespace chess {

class Position;
class TranspositionTable;
struct IterationInfo;

struct SearchLimits {
    int depth = 6;
    long long movetime_ms = 0;
    // When set, polled periodically during the search; setting it to true
    // (e.g. from a UCI "stop" command handled on another thread) aborts the
    // search early, same as a movetime deadline expiring.
    std::atomic<bool>* stop = nullptr;
    // Zobrist keys of every position played so far this game, from the
    // game's starting position through the position about to be searched
    // (inclusive), in order. Used to detect repetition draws anchored in
    // real game history, not just ones found within the search tree. Left
    // empty, the search still finds repeats made entirely of its own moves.
    std::vector<zobrist::Key> history;
    // Total node budget (negamax + quiescence combined) for the whole
    // search, checked at the same cadence as the movetime deadline. 0 means
    // unlimited (UCI "go nodes <N>").
    unsigned long long nodes_limit = 0;
    // Restrict the root to these moves only (UCI "go searchmoves ..."). An
    // empty vector (the default) means "search every legal root move" - and
    // so does a non-empty vector that matches none of them, as a defensive
    // fallback against a GUI listing moves that turned out illegal.
    std::vector<Move> search_moves;
    // Called once per completed iterative-deepening depth (see
    // IterationInfo). Left null (the default), the search doesn't report
    // progress at all - only the final SearchResult.
    std::function<void(const IterationInfo&)> on_iteration = nullptr;
};

struct SearchResult {
    Move best = MOVE_NONE;
    int score = 0;
    std::uint64_t nodes = 0;
    int depth = 0;
};

// Reported once per completed (non-aborted) iterative-deepening depth, so a
// caller (typically UCI's "go" handler) can print progress without
// search.cpp knowing anything about console I/O or move-string formatting.
// `multipv_index` is 0 for the best line; a caller requesting multiple
// principal variations (SearchLimits::multi_pv > 1, see the MultiPV search
// task) sees this fire once per reported line, in ascending index order.
struct IterationInfo {
    int multipv_index = 0;
    int depth = 0;
    int seldepth = 0;
    int score = 0;
    std::uint64_t nodes = 0;
    long long time_ms = 0;
    Move best = MOVE_NONE;
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
// `tt` is owned by the caller, not allocated here, so its contents persist
// across calls - e.g. across moves within the same game, if the caller
// keeps reusing the same table. Clear it explicitly
// (TranspositionTable::clear()) when stale entries should be discarded,
// such as on a new game.
SearchResult search_best_move(Position& pos, const SearchLimits& limits, TranspositionTable& tt);

}

#endif // CHESS_SEARCH_HPP
