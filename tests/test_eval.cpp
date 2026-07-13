#include "doctest.h"
#include "eval.hpp"
#include "position.hpp"
#include "attacks.hpp"
using namespace chess;

TEST_CASE("start position is roughly balanced") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(std::abs(evaluate(p)) <= 5);
}

TEST_CASE("being up a queen is strongly positive for side to move") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/8/8/8/3QK3 w - - 0 1"); // white has extra queen
    CHECK(evaluate(p) > 800);
}

TEST_CASE("evaluation is symmetric under side flip") {
    attacks::init();
    Position w; w.set("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    Position b; b.set("3qk3/8/8/8/8/8/8/4K3 b - - 0 1"); // mirror: black up a queen, black to move
    CHECK(evaluate(w) == evaluate(b));
}

TEST_CASE("EG king table rewards a centralized king over a cornered one in a bare-king endgame") {
    attacks::init();
    // Only kings on the board: material cancels and game_phase() is 0, so
    // this pins purely EG king-table behavior (the opposite polarity of
    // the old MG-only king PST, which preferred the back-rank corner).
    Position centralized; centralized.set("7k/8/8/8/4K3/8/8/8 w - - 0 1"); // White Ke4, Black Kh8
    Position cornered;    cornered.set("7k/8/8/8/8/8/8/K7 w - - 0 1");    // White Ka1, Black Kh8
    CHECK(evaluate(centralized) > evaluate(cornered));
}

TEST_CASE("pawn PST rewards an advanced pawn over a starting one") {
    attacks::init();
    // Identical except the White pawn's rank; kings fixed and cancel.
    Position advanced; advanced.set("k7/4P3/8/8/8/8/8/K7 w - - 0 1"); // White Pe7
    Position start;    start.set("k7/8/8/8/8/8/4P3/K7 w - - 0 1");    // White Pe2
    CHECK(evaluate(advanced) > evaluate(start));
}

TEST_CASE("king PST pins exact castled/exposed values") {
    attacks::init();
    // Only kings on the board: material cancels, so evaluate() reduces to
    // the EG king-table delta (game_phase() == 0 with no other pieces).
    // Pins exact values so a future transcription error (e.g. duplicated
    // rows) fails loudly instead of only being caught by a directional check.
    Position castled; castled.set("4k3/8/8/8/8/8/8/6K1 w - - 0 1");  // White Kg1
    CHECK(evaluate(castled) == 4);
    Position exposed; exposed.set("4k3/8/8/6K1/8/8/8/8 w - - 0 1");  // White Kg5
    CHECK(evaluate(exposed) == 54);
    Position symmetric; symmetric.set("6k1/8/8/8/8/8/8/6K1 w - - 0 1"); // both Kg1/Kg8
    CHECK(evaluate(symmetric) == 0);
}

TEST_CASE("a centralized knight has higher mobility than one boxed in by its own pawns") {
    attacks::init();
    // The knight sits on the *same* square (c5) in both positions. Black's
    // three pawns sit on the SAME squares (b7, d7, f7) in BOTH positions, so
    // Black's PST/material contribution cancels exactly between the two
    // evaluate() calls.
    //
    // All pawns (White's in both configs, and Black's) are confined to the
    // a-f files, deliberately avoiding g/h entirely. This closes a prior gap
    // where "boxed"'s White pawns (a4,b3,d3,e4) only cover files a-f via
    // adjacency, so a Black pawn on g/h could stand as an unblocked passed
    // pawn in "boxed" while being blocked in "mobile" by a White h-file pawn
    // - an asymmetric passed-pawn bonus that could make the CHECK below pass
    // even with mobility() deleted entirely. With every pawn kept within
    // a-f, each White pawn (in both "mobile" and "boxed") sits on the same
    // file or an adjacent file as one of Black's fixed pawns (b/d/f), and
    // vice versa, so no pawn - White or Black, in either config - is passed
    // (verified below by construction; no CHECK asserts it directly since
    // is_passed_pawn() is a private eval.cpp detail, not part of the public
    // API this test file includes).
    //
    // Confining files alone isn't sufficient, though: "mobile"'s White pawns
    // must also avoid the knight's eight attack squares (a4, a6, b3, b7, d3,
    // d7, e4, e6) to keep its mobility at 8, and merely picking "any" legal
    // rank for those pawns leaves a second, subtler asymmetry - the pawn PST
    // (MG and EG) value of a back-rank vs. a mid-board pawn differs by file
    // and rank in ways unrelated to mobility, and with only one minor piece
    // on the board game_phase() is just 1/24, so the endgame table (EG_PST)
    // dominates the taper almost entirely. Verified empirically (temporarily
    // zeroing mobility()'s WEIGHT array and rebuilding): an earlier version
    // of this test that put all four "mobile" pawns on rank 2 left a pure
    // PST edge in favor of "mobile" worth +15cp even with mobility() zeroed
    // out - i.e. the CHECK below would still pass with no mobility signal at
    // all. The ranks chosen below (b4, c4, d2, f4) were picked empirically so
    // that the PST/taper contribution alone very slightly favors "boxed"
    // (confirmed: with mobility zeroed, evaluate(boxed) > evaluate(mobile),
    // 393 > 388), so the real CHECK below only passes because mobility()
    // contributes its +16 (4 * (8 - 4)) swing in "mobile"'s favor (real
    // values: evaluate(mobile) = 420, evaluate(boxed) = 409). A knight on c5
    // attacks a4, a6, b3, b7, d3, d7, e4, e6 (8 squares).
    Position mobile; mobile.set("4k3/1p1p1p2/8/2N5/1PP2P2/8/3P4/4K3 w - - 0 1");
    // White pawns on b4, c4, f4, d2; Black pawns on b7, d7, f7. None of the
    // White pawns are among the knight's eight attack squares, so all 8
    // stay reachable (mobility = 8). Every White pawn file (b,c,d,f) is on
    // or adjacent to a Black pawn file (b,d,f), and vice versa, so no pawn
    // is passed. (b4/c4/f4/d2, rather than all-rank-2 or all-rank-4, is what
    // gives the PST/taper term its slight tilt toward "boxed" - see above.)
    Position boxed; boxed.set("4k3/1p1p1p2/8/2N5/P3P3/1P1P4/8/4K3 w - - 0 1");
    // White pawns on a4, b3, d3, e4; Black pawns on b7, d7, f7 (same squares
    // as in "mobile", so they cancel exactly). The four White pawns occupy
    // exactly four of the knight's eight attack squares (a4, b3, d3, e4),
    // leaving only a6, b7, d7, e6 reachable (mobility = 4). Every White pawn
    // file (a,b,d,e) is on or adjacent to a Black pawn file (b,d,f), and
    // vice versa, so no pawn is passed here either.
    CHECK(evaluate(mobile) > evaluate(boxed));
}

TEST_CASE("the bishop pair is worth a bonus over a lone bishop") {
    attacks::init();
    // White's lone king is held on the same square (g1) in both positions,
    // so its contribution cancels out between the two evaluate() calls;
    // Black has a bishop+knight vs bishop+bishop, isolating the pair bonus.
    // Bishop and knight MG material values are close enough (365 vs 337)
    // that the direction of the check isolates the pair bonus rather than
    // being dominated by the material gap.
    Position pair;    CHECK(pair.set("4k3/8/8/8/8/2b2b2/8/6K1 b - - 0 1") == true);    // Black bishops c3,f3
    Position no_pair; CHECK(no_pair.set("4k3/8/8/8/8/2n2b2/8/6K1 b - - 0 1") == true); // Black knight+bishop c3,f3
    // Black to move in both, and evaluate() already returns the score from
    // the side-to-move's (Black's) perspective, so no extra negation is
    // needed: a higher value directly means better for Black.
    CHECK(evaluate(pair) > evaluate(no_pair));
}

TEST_CASE("a rook on a fully open file outscores one blocked by its own pawn") {
    attacks::init();
    // The rook's mobility is boxed to exactly 0 in BOTH positions: King d2
    // blocks it immediately to the north, Bishop c1 blocks it immediately to
    // the west, Bishop e1 blocks it immediately to the east, and rank 1's
    // south edge needs no blocker. So nothing on the d-file beyond d2 (or
    // anywhere else) can change the rook's mobility() contribution - only
    // rook_file_bonus()'s whole-file scan (which inspects the entire file,
    // not just squares the rook can see) can pick up a pawn placed there.
    // The bishops' own diagonals are also unaffected: c1's only open
    // diagonal (b2/a3) and e1's only open diagonal (f2/g3/h4) never cross
    // the d-file or h3/d3 (the two pawn squares used below), so bishop
    // mobility is identical in both positions too.
    // The lone White pawn sits on h3 in "open" (off the d-file entirely, so
    // rook_file_bonus() sees no own pawn on d -> ROOK_OPEN_FILE_BONUS) and on
    // d3 in "blocked" (same file as the rook, one square behind the king
    // that already blocks the rook's view -> no bonus). Moving the pawn
    // between h3/d3 (same rank) leaves material identical and leaves only a
    // small pawn-PST residual (verified empirically below to be a few
    // centipawns against the "open" side), so the ~20cp rook_file_bonus is
    // what decides the comparison, not an artifact of mobility or PST.
    Position open;    CHECK(open.set("k7/8/8/8/8/7P/3K4/2BRB3 w - - 0 1") == true);    // Kd2,Bc1,Rd1,Be1, pawn h3; Black Ka8
    Position blocked; CHECK(blocked.set("k7/8/8/8/8/3P4/3K4/2BRB3 w - - 0 1") == true); // same, pawn d3 instead of h3
    int open_eval = evaluate(open);
    int blocked_eval = evaluate(blocked);
    // rook_file_bonus() differs (open: +20, blocked: +0); mobility and
    // bishop pair are identical between the two, and the pawn-PST residual
    // (a few cp against "open", per the comment above) is smaller than the
    // bonus, so the bonus is what decides the direction of this CHECK.
    CHECK(open_eval > blocked_eval);
}

TEST_CASE("an unopposed passed pawn outscores one that can be blocked or captured") {
    attacks::init();
    // Same material (K+P+decoy-P each side) and same White pawn square (d5)
    // in both; "passed" gets a decoy Black pawn on a6 (a file that doesn't
    // block/capture the d-file pawn, so White's pawn is still passed, but
    // material is now equal in both positions), while "blocked" has a Black
    // pawn on d6 directly ahead of the White pawn.
    Position passed; CHECK(passed.set("4k3/8/p7/3P4/8/8/8/4K3 w - - 0 1"));   // White pawn d5 (passed); Black decoy pawn a6 (doesn't block)
    Position blocked; CHECK(blocked.set("4k3/8/3p4/3P4/8/8/8/4K3 w - - 0 1")); // White pawn d5 (blocked); Black pawn d6 blocks it
    CHECK(evaluate(passed) > evaluate(blocked));
}

TEST_CASE("a more advanced passed pawn outscores a less advanced one") {
    attacks::init();
    // Isolate the passed-pawn bonus's rank-scaling from the pawn PST's own
    // rank-scaling (both increase with rank, so a direct passed-vs-passed
    // comparison at different ranks can't isolate the bonus alone) by
    // comparing (passed - blocked) deltas at two different ranks instead.
    // The decoy pawn (a4) is identical in both "passed" positions, and the
    // blocker pawn (d7) is identical in both "blocked" positions - so its
    // PST/material contribution cancels exactly between the two deltas -
    // and the only thing that can make one delta bigger than the other is
    // the passed-pawn bonus table's rank-dependent value. (d7, not d8: a
    // pawn can never legally sit on the back rank in this engine -
    // Position::set() rejects it - so d7 is used as the nearest square that
    // still blocks both the d6 and d3 White pawns.)
    Position adv_passed;  CHECK(adv_passed.set("4k3/8/3P4/8/p7/8/8/4K3 w - - 0 1"));  // White Pd6 (passed), Black decoy Pa4
    Position adv_blocked; CHECK(adv_blocked.set("4k3/3p4/3P4/8/8/8/8/4K3 w - - 0 1"));  // White Pd6 (blocked by Black Pd7)
    Position early_passed;  CHECK(early_passed.set("4k3/8/8/8/p7/3P4/8/4K3 w - - 0 1"));  // White Pd3 (passed), Black decoy Pa4
    Position early_blocked; CHECK(early_blocked.set("4k3/3p4/8/8/8/3P4/8/4K3 w - - 0 1"));  // White Pd3 (blocked by Black Pd7, same square as adv_blocked's blocker)

    int delta_advanced = evaluate(adv_passed) - evaluate(adv_blocked);
    int delta_early = evaluate(early_passed) - evaluate(early_blocked);
    CHECK(delta_advanced > delta_early);
}

TEST_CASE("king_safety() adds bonus for pawns on shield squares") {
    attacks::init();
    // Both positions below are IDENTICAL except for White's king square (g1
    // vs b1): the three White pawns (f2/g2/h2), the Black king (e8), and the
    // two queens + two rooks per side (c4/d4/e4/f4 and c5/d5/e5/f5) sit on
    // the exact same squares in both FENs. That extra major material also
    // pushes game_phase() to its max (24 = 2*(2*4 queen + 2*2 rook)), which
    // makes taper() collapse to the MG score alone, so the EG king table
    // (which also varies by king square) cannot leak into the comparison.
    //
    // - King on g1: king_safety()'s 3-file x 2-rank shield zone (f/g/h,
    //   ranks 2-3) covers all three pawns -> shield bonus = 3*KING_SHIELD_BONUS.
    // - King on b1: the shield zone (a/b/c, ranks 2-3) contains none of the
    //   f2/g2/h2 pawns -> shield bonus = 0.
    //
    // The two king squares also differ in MG king-PST value (g1 < b1), which
    // works AGAINST the shielded position, so this isn't a free confound in
    // king_safety()'s favor - the shield bonus has to outweigh it. Verified
    // empirically: with the real KING_SHIELD_BONUS (10), g1 evaluates to 330
    // and b1 to 311 (g1 wins by 19); with KING_SHIELD_BONUS temporarily set
    // to 0, g1 drops to 300 while b1 stays at 311 (b1 now wins), proving the
    // shielded > pushed direction below is decided by king_safety() itself,
    // not by the residual king-PST difference.
    Position shielded; CHECK(shielded.set("4k3/8/8/2rqqr2/2RQQR2/8/5PPP/6K1 w - - 0 1"));  // White Kg1 (shielded by f2/g2/h2)
    Position exposed;   CHECK(exposed.set("4k3/8/8/2rqqr2/2RQQR2/8/5PPP/1K6 w - - 0 1"));  // White Kb1 (f2/g2/h2 outside shield zone)
    CHECK(evaluate(shielded) > evaluate(exposed));
}
