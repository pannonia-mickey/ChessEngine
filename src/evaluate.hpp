#pragma once

#include "position.hpp"

namespace chess {

// Scores are in centipawns from the side to move's point of view.
using Score = int;

inline constexpr Score kDrawScore = 0;

// Static evaluation: material plus piece-square tables, tapered between middlegame and endgame
// values by the remaining non-pawn material. The values are PeSTO's (Ronald Friederich), a strong
// hand-crafted baseline for later tuning.
[[nodiscard]] Score evaluate(const Position& pos);

// The game phase in [0, kMaxPhase]: kMaxPhase with all minor and major pieces on the board, 0
// with only kings and pawns.
inline constexpr int kMaxPhase = 24;
[[nodiscard]] int game_phase(const Position& pos);

// Middlegame material value of a piece type, used by move ordering.
[[nodiscard]] Score piece_value(PieceType type);

}  // namespace chess
