#pragma once

#include <array>

#include "eval_params.hpp"
#include "position.hpp"

namespace chess {

inline constexpr Score kDrawScore = 0;

// Static evaluation in centipawns from the side to move's point of view: material, piece-square
// tables, mobility, pawn structure, passed pawns, a few piece terms and king safety, tapered
// between middlegame and endgame values by the remaining non-pawn material. The parameters are
// in eval_values.hpp, tuned with tools/tuner starting from PeSTO's (Ronald Friederich) material
// and piece-square tables.
[[nodiscard]] Score evaluate(const Position& pos);

// The game phase in [0, kMaxPhase]: kMaxPhase with all minor and major pieces on the board, 0
// with only kings and pawns.
inline constexpr int kMaxPhase = 24;
[[nodiscard]] int game_phase(const Position& pos);

// Middlegame material value of a piece type, used by move ordering.
[[nodiscard]] Score piece_value(PieceType type);

// The evaluation as a linear function of the parameters, for the tuner: how often each
// parameter's feature occurs for White minus how often for Black. With the parameters' values
// the White point of view score is the middlegame sum times phase plus the endgame sum times
// (kMaxPhase - phase), divided by kMaxPhase.
struct EvalTrace {
    std::array<int, eval::kParamCount> coefficients{};
    int phase = 0;
};
[[nodiscard]] EvalTrace trace_evaluation(const Position& pos);

}  // namespace chess
