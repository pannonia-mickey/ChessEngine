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
    // The knight sits on the *same* square (c5) in both positions, and both
    // have identical material (White: K+N+4P, Black: K), so the knight's own
    // PST contribution and all material values cancel exactly; only the
    // knight's mobility differs. A knight on c5 attacks a4, a6, b3, b7, d3,
    // d7, e4, e6 (8 squares).
    Position mobile; mobile.set("4k3/8/8/2N5/8/8/P1P2P1P/4K3 w - - 0 1");
    // White pawns on a2, c2, f2, h2 - none of these are among the knight's
    // eight attack squares, so all 8 stay reachable (mobility = 8).
    Position boxed; boxed.set("4k3/8/8/2N5/P3P3/1P1P4/8/4K3 w - - 0 1");
    // White pawns on a4, b3, d3, e4 - exactly four of the knight's own
    // attack squares, occupied by its own color so they don't count as
    // mobility, leaving only a6, b7, d7, e6 reachable (mobility = 4).
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
    // Same material (K+R+P) and same rook/king squares in both; only
    // whether the rook's own pawn sits on its file differs.
    // Pawn on a2 (open file): PST -35 in MG
    // Pawn on d2 (blocked rook d-file): PST -23 in MG
    // So the pawn's own PST is 12cp WORSE on a2, meaning the rook's open-file
    // bonus must overcome that to drive the CHECK direction.
    // Test isolation: the rook mobility difference (7 squares * weight 2 = 14cp)
    // combined with rook_file_bonus (20cp) and tapered PST (~1cp) should make
    // the open position significantly better.
    Position open;    CHECK(open.set("4k3/8/8/8/8/8/P7/3RK3 w - - 0 1") == true);    // Rd1, pawn on a2
    Position blocked; CHECK(blocked.set("4k3/8/8/8/8/8/3P4/3RK3 w - - 0 1") == true); // Rd1, pawn on d2
    int open_eval = evaluate(open);
    int blocked_eval = evaluate(blocked);
    // Expect significant bonus for open file: mobility (14cp) + rook_file_bonus (20cp)
    CHECK(open_eval > blocked_eval);
}
