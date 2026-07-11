#pragma once

#include "position.hpp"
#include <cstdint>

namespace chess {

// Count the number of leaf nodes reachable from `pos` at exactly `depth`
// plies of search, using only legal moves. Mutates `pos` via do_move/undo_move
// internally but restores it fully before returning.
std::uint64_t perft(Position& pos, int depth);

} // namespace chess
