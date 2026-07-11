#include "doctest.h"
#include "attacks.hpp"
using namespace chess;

TEST_CASE("non-sliding attacks") {
    attacks::init();
    // Knight on d4 attacks 8 squares.
    CHECK(popcount(knight_attacks(SQ_D4)) == 8);
    // Knight on a1 attacks exactly b3 and c2.
    CHECK(knight_attacks(SQ_A1) == (square_bb(SQ_B3) | square_bb(SQ_C2)));
    // King on d4 attacks 8, king on a1 attacks 3.
    CHECK(popcount(king_attacks(SQ_D4)) == 8);
    CHECK(popcount(king_attacks(SQ_A1)) == 3);
    // White pawn on e2 attacks d3 and f3.
    CHECK(pawn_attacks(WHITE, SQ_E2) == (square_bb(SQ_D3) | square_bb(SQ_F3)));
    // Black pawn on a7 attacks only b6.
    CHECK(pawn_attacks(BLACK, SQ_A7) == square_bb(SQ_B6));
}

TEST_CASE("sliding attacks via magics") {
    attacks::init();
    // Rook on a1, empty board: whole file a + rank 1 minus a1 = 14 squares.
    CHECK(popcount(rook_attacks(SQ_A1, 0ULL)) == 14);
    // Rook on a1 blocked by own pawn on a2 and h1-side blocker on c1.
    Bitboard occ = square_bb(SQ_A4) | square_bb(SQ_C1);
    Bitboard ra = rook_attacks(SQ_A1, occ);
    CHECK((ra & square_bb(SQ_A4)) != 0ULL);   // sees the blocker
    CHECK((ra & square_bb(SQ_A5)) == 0ULL);   // stops at it
    CHECK((ra & square_bb(SQ_C1)) != 0ULL);
    CHECK((ra & square_bb(SQ_D1)) == 0ULL);
    // Bishop on d4 empty board attacks 13 squares.
    CHECK(popcount(bishop_attacks(SQ_D4, 0ULL)) == 13);
    // Queen = rook | bishop.
    CHECK(queen_attacks(SQ_D4, 0ULL) ==
          (rook_attacks(SQ_D4, 0ULL) | bishop_attacks(SQ_D4, 0ULL)));
}
