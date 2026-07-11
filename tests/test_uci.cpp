#include "doctest.h"
#include "uci.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "types.hpp"
#include "search.hpp"
using namespace chess;

TEST_CASE("move <-> uci string") {
    attacks::init();
    CHECK(move_to_uci(make_move(SQ_E2, SQ_E4)) == "e2e4");
    CHECK(move_to_uci(make_move(SQ_A7, SQ_A8, PROMOTION, QUEEN)) == "a7a8q");
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(uci_to_move(p, "e2e4") == make_move(SQ_E2, SQ_E4));
}

TEST_CASE("format_score formats centipawn and mate scores per the UCI spec") {
    CHECK(format_score(37) == "cp 37");
    CHECK(format_score(-120) == "cp -120");
    CHECK(format_score(0) == "cp 0");
    // Mate found for the side to move: MATE - ply, e.g. mate in 1 ply (mate
    // in 1 move) scores MATE - 1.
    CHECK(format_score(MATE - 1) == "mate 1");
    // Mate in 4 plies == mate in 2 moves.
    CHECK(format_score(MATE - 4) == "mate 2");
    // Getting mated (negative): symmetric, negative move count.
    CHECK(format_score(-(MATE - 1)) == "mate -1");
    CHECK(format_score(-(MATE - 4)) == "mate -2");
}

TEST_CASE("compute_move_time budgets the clock") {
    // Sudden death, 60s each, White to move: 60000/30 - 30 overhead = 1970.
    CHECK(compute_move_time(WHITE, 60000, 60000, 0, 0, 0) == 1970);
    // Uses the side-to-move's own clock: Black low on time.
    CHECK(compute_move_time(BLACK, 60000, 1000, 0, 0, 0) == 3);
    // movestogo=1 but capped at 80% of the clock (1000*4/5 - 30 = 770).
    CHECK(compute_move_time(WHITE, 1000, 1000, 0, 0, 1) == 770);
    // Increment adds 3/4 of the increment: 10000/30 + 750 - 30 = 1053.
    CHECK(compute_move_time(WHITE, 10000, 10000, 1000, 1000, 0) == 1053);
    // No time on our clock -> minimal positive budget.
    CHECK(compute_move_time(WHITE, 0, 60000, 0, 0, 0) == 1);
}
