#pragma once
#include <cstdint>
#include "types.hpp"
namespace chess {
using Move = std::uint16_t;
enum MoveFlag : int { NORMAL = 0, PROMOTION = 1, EN_PASSANT = 2, CASTLING = 3 };
constexpr Move MOVE_NONE = 0;
constexpr Move make_move(Square from, Square to) {
  return Move(int(from) | (int(to) << 6));
}
constexpr Move make_move(Square from, Square to, MoveFlag f, PieceType promo = KNIGHT) {
  return Move(int(from) | (int(to) << 6) | ((int(promo) - int(KNIGHT)) << 12) | (int(f) << 14));
}
constexpr Square from_sq(Move m) { return Square(m & 0x3F); }
constexpr Square to_sq(Move m) { return Square((m >> 6) & 0x3F); }
constexpr MoveFlag flag_of(Move m) { return MoveFlag((m >> 14) & 0x3); }
constexpr PieceType promo_type(Move m) { return PieceType(((m >> 12) & 0x3) + int(KNIGHT)); }
} // namespace chess
