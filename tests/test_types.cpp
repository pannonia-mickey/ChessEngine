#include "doctest.h"
#include "types.hpp"
#include "move.hpp"
using namespace chess;

TEST_CASE("square mapping is LERF") {
    CHECK(make_square(0, 0) == SQ_A1);
    CHECK(make_square(7, 0) == SQ_H1);
    CHECK(make_square(0, 7) == SQ_A8);
    CHECK(file_of(SQ_H8) == 7);
    CHECK(rank_of(SQ_H8) == 7);
}

TEST_CASE("move round-trips through encoding") {
    Move m = make_move(SQ_E2, SQ_E4);
    CHECK(from_sq(m) == SQ_E2);
    CHECK(to_sq(m) == SQ_E4);
    CHECK(flag_of(m) == NORMAL);

    Move promo = make_move(SQ_A7, SQ_A8, PROMOTION, QUEEN);
    CHECK(from_sq(promo) == SQ_A7);
    CHECK(to_sq(promo) == SQ_A8);
    CHECK(flag_of(promo) == PROMOTION);
    CHECK(promo_type(promo) == QUEEN);
}

TEST_CASE("piece type/color extraction") {
    CHECK((B_QUEEN & 7) == QUEEN);
    CHECK((B_QUEEN >> 3) == BLACK);
    CHECK((W_KNIGHT & 7) == KNIGHT);
    CHECK((W_KNIGHT >> 3) == WHITE);
}
