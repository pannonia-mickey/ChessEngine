#include "doctest.h"
#include "bitboard.hpp"
using namespace chess;

TEST_CASE("square_bb and popcount") {
    CHECK(square_bb(SQ_A1) == 1ULL);
    CHECK(square_bb(SQ_H8) == (1ULL << 63));
    CHECK(popcount(0ULL) == 0);
    CHECK(popcount(0xFFULL) == 8);
}

TEST_CASE("pop_lsb walks set bits low to high") {
    Bitboard b = square_bb(SQ_C1) | square_bb(SQ_A2);
    CHECK(pop_lsb(b) == SQ_C1);
    CHECK(pop_lsb(b) == SQ_A2);
    CHECK(b == 0ULL);
}

TEST_CASE("shifts do not wrap around board edges") {
    CHECK(shift_east(square_bb(SQ_H1)) == 0ULL);   // off the right edge
    CHECK(shift_west(square_bb(SQ_A1)) == 0ULL);   // off the left edge
    CHECK(shift_north(square_bb(SQ_A8)) == 0ULL);  // off the top
    CHECK(shift_east(square_bb(SQ_A1)) == square_bb(SQ_B1));
}

TEST_CASE("diagonal shifts move correctly and do not wrap around board edges") {
    // From d4, each diagonal shift lands on the expected neighbor.
    CHECK(shift_north_east(square_bb(SQ_D4)) == square_bb(SQ_E5));
    CHECK(shift_north_west(square_bb(SQ_D4)) == square_bb(SQ_C5));
    CHECK(shift_south_east(square_bb(SQ_D4)) == square_bb(SQ_E3));
    CHECK(shift_south_west(square_bb(SQ_D4)) == square_bb(SQ_C3));

    // Edge cases: shifting off the board must yield 0, not wrap to the opposite file.
    CHECK(shift_north_east(square_bb(SQ_H4)) == 0ULL); // off the right edge
    CHECK(shift_north_west(square_bb(SQ_A4)) == 0ULL); // off the left edge
    CHECK(shift_south_east(square_bb(SQ_H4)) == 0ULL); // off the right edge
    CHECK(shift_south_west(square_bb(SQ_A4)) == 0ULL); // off the left edge
    CHECK(shift_north_east(square_bb(SQ_A8)) == 0ULL); // off the top
    CHECK(shift_south_west(square_bb(SQ_H1)) == 0ULL); // off the bottom
}
