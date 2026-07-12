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
