#pragma once

#include <cstdint>
#include <vector>

#include "move.hpp"
#include "position.hpp"

namespace chess {

// Counts the leaf nodes of the legal move tree to the given depth. The standard way to verify
// move generation against known node counts.
[[nodiscard]] std::uint64_t perft(Position& pos, int depth);

struct PerftEntry {
    Move move;
    std::uint64_t nodes = 0;
};

// perft split by root move, for locating move generation bugs.
[[nodiscard]] std::vector<PerftEntry> perft_divide(Position& pos, int depth);

}  // namespace chess
