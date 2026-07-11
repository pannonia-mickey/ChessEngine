#pragma once

#include <string>
#include "move.hpp"
#include "types.hpp"

namespace chess {

class Position;

// Run the UCI command loop, reading commands from stdin until EOF or "quit".
void uci_loop();

// Convert a move to long algebraic coordinate notation (e.g. "e2e4", "e7e8q").
std::string move_to_uci(Move m);

// Parse a UCI coordinate move string against the legal moves in `pos`,
// returning the matching Move (with correct flags) or MOVE_NONE if illegal.
// Takes `pos` by non-const reference because generate_legal() applies/undoes
// moves on it in place; `pos` is left unchanged once this function returns.
Move uci_to_move(Position& pos, const std::string& s);

// Derive a per-move time budget (ms) from the clock for the side to move.
// Pure/deterministic so it can be unit-tested independently of the UCI loop.
long long compute_move_time(Color us, long long wtime, long long btime,
                            long long winc, long long binc, int movestogo);

// Format a search score as a UCI "info ... score <this>" field: "cp N" for
// a normal centipawn score, or "mate N" (N = moves, not plies, to mate;
// negative when the side to move is the one getting mated) for scores in
// the mate range.
std::string format_score(int score);

} // namespace chess
