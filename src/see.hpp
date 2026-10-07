#pragma once

#include <array>

#include "move.hpp"
#include "position.hpp"
#include "types.hpp"

namespace chess {

// Static exchange evaluation: whether playing `move` wins at least `threshold` centipawns of
// material once every capture on its target square has been played out, each side always
// capturing with its least valuable piece and free to stop when continuing would lose material.
// Pins and checks are ignored. Castling and quiet moves exchange nothing unless the moved piece
// can be taken; a promotion counts as gaining the promoted piece in place of the pawn.
[[nodiscard]] bool see_ge(const Position& pos, Move move, int threshold);

// The material values the exchange is counted in. Classical values rather than the tuned
// evaluation's: an exchange only needs their order and rough ratios, and the king is worth more
// than everything else together, so it never captures into a defended square.
[[nodiscard]] constexpr int see_value(PieceType type) noexcept {
    constexpr std::array<int, kPieceTypeCount + 1> kValues = {100, 320, 330, 500, 950, 20'000, 0};
    return kValues[type];
}

}  // namespace chess
