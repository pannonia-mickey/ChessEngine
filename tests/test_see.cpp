#include "doctest.h"
#include "see.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "move.hpp"
using namespace chess;

TEST_CASE("SEE: undefended capture returns the captured piece's value") {
    attacks::init();
    // White queen d1, Black pawn d7, nothing else attacks d7.
    Position p; CHECK(p.set("7k/3p4/8/8/8/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D1, SQ_D7)) == 100);
}

TEST_CASE("SEE: losing capture returns a negative value when the defender wins the exchange") {
    attacks::init();
    // White queen d1 captures the pawn on d7, but Black's knight on b8
    // (the only defender) recaptures the queen for free afterwards:
    // net = pawn(100) - queen(900) = -800.
    Position p; CHECK(p.set("1n5k/3p4/8/8/8/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D1, SQ_D7)) == -800);
}

TEST_CASE("SEE: even rook trade nets zero") {
    attacks::init();
    // White rook a1 takes the rook on a8; Black's rook on h8 recaptures
    // along rank 8. Rook for rook: net = 0.
    Position p; CHECK(p.set("r6r/7k/8/8/8/8/8/R6K w - - 0 1"));
    CHECK(see(p, make_move(SQ_A1, SQ_A8)) == 0);
}
