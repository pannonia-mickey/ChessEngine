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

TEST_CASE("SEE: declines a further capture that would lose material (stops the exchange early)") {
    attacks::init();
    // White pawn d4 takes the pawn on e5. Black recaptures with the
    // knight on c6 (regaining the pawn: net so far 0). White *could*
    // recapture the knight with the queen on e1, but Black's rook on e8
    // would then win the queen for a rook - a bad trade for White, who
    // therefore should decline it and stop. Final result: 0, not the
    // -580 a naive "always recapture" implementation would compute.
    Position p; CHECK(p.set("4r2k/8/2n5/4p3/3P4/8/8/K3Q3 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D4, SQ_E5)) == 0);
}

TEST_CASE("SEE: reveals an x-ray attacker behind a captured piece") {
    attacks::init();
    // White rook d3 takes the knight on d5 (+320). Black's pawn on c6
    // recaptures the rook (+500 for Black). White's second rook on d1,
    // previously blocked by the d3 rook, is now unblocked and recaptures
    // the pawn (+100 for White). Net for White: 320 - 500 + 100 = -80.
    // An implementation that fails to re-reveal the d1 rook after d3 is
    // removed would instead stop after Black's recapture and return
    // -180, so this distinguishes a working x-ray from a broken one.
    Position p; CHECK(p.set("7k/8/2p5/3n4/8/3R4/8/K2R4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D3, SQ_D5)) == -80);
}

TEST_CASE("SEE: en passant capture uses the actual captured pawn's square") {
    attacks::init();
    // White pawn e5 captures en passant onto d6, removing Black's pawn
    // actually sitting on d5 (not on d6). Black's rook on d1 can only
    // attack d6 - and thus recapture - once d5 is genuinely cleared from
    // the exchange's occupancy: with d5 vacated, the rook's file is open
    // all the way to d6 (500-value rook recaptures the 100-value pawn,
    // netting 0 overall: 100 - 100 = 0). An implementation that mistakenly
    // cleared d6 (the move's destination) instead of d5 (the actual
    // captured square) would leave d5 "occupied" in the simulated
    // occupancy, blocking the rook's file, and would wrongly compute 100
    // (as if the rook were never there) instead of the correct 0.
    Position p; CHECK(p.set("7k/8/8/3pP3/8/8/K7/3r4 w - d6 0 1"));
    CHECK(see(p, make_move(SQ_E5, SQ_D6, EN_PASSANT)) == 0);
}

TEST_CASE("SEE: recaptures with the least valuable attacker, not just any attacker") {
    attacks::init();
    // White pawn e4 takes the knight on d5 (+320). Black has two possible
    // recapturing pieces on d5: the pawn on c6 (100) and the queen on d8
    // (900, via the d-file) - a correct implementation always continues
    // with the *cheaper* one available (the pawn), not just any attacker.
    // After Black recaptures with the pawn, White's rook on d1 could take
    // that pawn (+100), but Black's queen would then win the rook for
    // free (+500 for Black) with nothing further to punish it - a bad
    // continuation White correctly declines. Net: White wins the knight
    // and loses only the initiating pawn: 320 - 100 = 220.
    Position p; CHECK(p.set("3q3k/8/2p5/3n4/4P3/8/K7/3R4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_E4, SQ_D5)) == 220);
}

TEST_CASE("SEE: a winning multi-round exchange nets the full accumulated gain") {
    attacks::init();
    // White knight f4 takes the bishop on d5 (+330). Black's knight on b6
    // recaptures (Black regains 320, matching White's knight now sitting
    // on d5). White's queen on d1 then captures that knight (+320 more)
    // with no further Black attacker left to punish it, so continuing is
    // pure profit and the algorithm correctly does not decline. Net:
    // 330 (bishop) - 320 (White's knight, recaptured) + 320 (Black's
    // knight, captured by the queen) = 330 - a genuine multi-ply gain,
    // not just a single free capture.
    Position p; CHECK(p.set("7k/8/1n6/3b4/5N2/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_F4, SQ_D5)) == 330);
}
