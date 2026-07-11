#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "move.hpp"

namespace chess {

enum TTBound : std::uint8_t { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    std::uint64_t key = 0;
    int score = 0;
    int depth = -1;
    TTBound bound = TT_NONE;
    Move best = MOVE_NONE;
};

// Fixed-size, always-replace transposition table (simplest correct
// replacement policy; a depth-preferred scheme can be tuned later behind its
// own SPRT if profiling justifies it). Sized once at construction from a
// megabyte budget, rounded down to a power of two so probing can use a mask
// instead of a modulo.
class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t mb);

    void clear();
    const TTEntry* probe(std::uint64_t key) const;
    void store(std::uint64_t key, int depth, int score, TTBound bound, Move best);

private:
    std::vector<TTEntry> table_;
    std::size_t mask_;
};

} // namespace chess
