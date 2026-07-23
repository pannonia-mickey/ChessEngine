#pragma once

#include "move.hpp"
#include "position.hpp"

namespace chess {

struct MoveList {
    Move moves[256];
    int size = 0;
    // Bounded so a pathological position (e.g. a crafted FEN with far more
    // queens than a real game could produce) can't overflow the fixed array.
    void add(Move m) { if (size < 256) moves[size++] = m; }
    Move* begin() { return moves; }
    Move* end() { return moves + size; }
};

// Generate all pseudo-legal moves for the side to move (does not check
// whether the mover's own king ends up in check).
void generate_pseudo(const Position& pos, MoveList& list);

// Generate all legal moves for the side to move. Takes `pos` by non-const
// reference because it applies/undoes each candidate move in place (via
// do_move/undo_move) to test king safety, restoring it before returning.
// `in_check_out` receives whether the side to move is currently in check -
// generate_legal() already computes this (as part of its checkers bitboard)
// to build the resolve-check mask, so callers that would otherwise call
// Position::square_attacked_by() separately for the same answer can reuse it
// instead of re-scanning.
void generate_legal(Position& pos, MoveList& list, bool& in_check_out);
inline void generate_legal(Position& pos, MoveList& list) {
    bool ignored;
    generate_legal(pos, list, ignored);
}

} // namespace chess
