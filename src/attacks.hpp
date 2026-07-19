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

// Squares strictly between s1 and s2 (both endpoints excluded) if they share
// a rank, file, or diagonal; 0 otherwise. Computed on demand from the magic
// tables via the standard double-ray-intersection trick: sliding from each
// endpoint with the other endpoint as the sole blocker, then intersecting,
// isolates exactly the shared-line squares strictly in between - but only
// once alignment is confirmed on an empty board first. Skipping that check
// and blindly OR-ing the rook and bishop versions together is unsound: for
// squares that aren't aligned at all, the two squares' *unrelated* ray
// directions (e.g. one's file ray and the other's rank ray) can still cross
// at a shared point, producing a spurious non-zero result.
inline Bitboard between_bb(Square s1, Square s2) {
    Bitboard b1 = square_bb(s1);
    Bitboard b2 = square_bb(s2);
    if (rook_attacks(s1, 0) & b2) {
        return rook_attacks(s1, b2) & rook_attacks(s2, b1);
    }
    if (bishop_attacks(s1, 0) & b2) {
        return bishop_attacks(s1, b2) & bishop_attacks(s2, b1);
    }
    return 0;
}

} // namespace chess
