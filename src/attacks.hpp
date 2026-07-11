#pragma once

#include "bitboard.hpp"
#include "types.hpp"

namespace chess {

namespace attacks {
    // Initialize all attack tables.
    void init();
} // namespace attacks

// Query functions for non-sliding piece attacks.
Bitboard pawn_attacks(Color c, Square s);
Bitboard knight_attacks(Square s);
Bitboard king_attacks(Square s);

// Query functions for sliding piece attacks (magic bitboards).
Bitboard bishop_attacks(Square s, Bitboard occupied);
Bitboard rook_attacks(Square s, Bitboard occupied);

inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}

} // namespace chess
