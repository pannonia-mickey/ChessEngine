#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "see.hpp"
#include "test_helpers.hpp"

using namespace chess;

namespace {

// Whether the move's exchange wins exactly `expected`: at least it, and not one centipawn more.
bool see_equals(std::string_view fen, std::string_view move_text, int expected) {
    const Position pos = test::position_from(fen);
    const Move move = test::legal_move(pos, move_text);
    return see_ge(pos, move, expected) && !see_ge(pos, move, expected + 1);
}

constexpr int kPawn = see_value(Pawn);
constexpr int kKnight = see_value(Knight);
constexpr int kBishop = see_value(Bishop);
constexpr int kRook = see_value(Rook);
constexpr int kQueen = see_value(Queen);

}  // namespace

TEST_CASE("SEE of an undefended capture is the captured piece", "[see]") {
    CHECK(see_equals("1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", "e1e5", kPawn));
    CHECK(see_equals("4k3/8/8/3q4/8/2N5/8/4K3 w - - 0 1", "c3d5", kQueen));
}

TEST_CASE("SEE subtracts the capturing piece when it is recaptured", "[see]") {
    // Rxd7 Rxd7: a pawn for a rook.
    CHECK(see_equals("3r2k1/3p4/8/8/8/8/8/3R2K1 w - - 0 1", "d1d7", kPawn - kRook));
    // Nxe5 dxe5: a pawn for a knight.
    CHECK(see_equals("4k3/8/3p4/4p3/8/5N2/8/4K3 w - - 0 1", "f3e5", kPawn - kKnight));
}

TEST_CASE("SEE lets a side stop the exchange when continuing loses", "[see]") {
    // Bxe5 dxe5, and white stops: Qxe5 Rxe5 would lose the queen as well.
    CHECK(see_equals("4r1k1/8/3p4/4p3/8/2B5/8/4QK2 w - - 0 1", "c3e5", kPawn - kBishop));
    // Pawn takes a defended knight: the pawn is lost but the knight is won.
    CHECK(see_equals("4k3/8/4p3/3n4/4P3/8/8/4K3 w - - 0 1", "e4d5", kKnight - kPawn));
}

TEST_CASE("SEE counts sliders hidden behind other attackers", "[see]") {
    // Rxe5 Rxe5 Rxe5: the rook doubled behind on the e-file wins the pawn after the trade.
    CHECK(see_equals("4r1k1/8/8/4p3/8/8/4R3/4R1K1 w - - 0 1", "e2e5", kPawn));
    // Bxe5 dxe5 Qxe5: the queen behind the bishop wins back a pawn for it.
    CHECK(see_equals("6k1/8/3p4/4p3/3B4/2Q5/8/6K1 w - - 0 1", "d4e5", (2 * kPawn) - kBishop));
}

TEST_CASE("SEE does not let the king capture into a defended square", "[see]") {
    // Rxe2 Kxe2: the king takes back an undefended rook.
    CHECK(see_equals("4r1k1/8/8/8/8/8/4P3/4K3 b - - 0 1", "e8e2", kPawn - kRook));
    // With a second rook behind, the king cannot take back.
    CHECK(see_equals("4r1k1/4r3/8/8/8/8/4P3/4K3 b - - 0 1", "e7e2", kPawn));
}

TEST_CASE("SEE of en passant and promotions", "[see]") {
    CHECK(see_equals("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", kPawn));
    // An undefended promotion gains a queen for the pawn.
    CHECK(see_equals("4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", kQueen - kPawn));
    // A promotion onto a defended square loses the pawn.
    CHECK(see_equals("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", -kPawn));
    // Capturing the rook while promoting wins it.
    CHECK(see_equals("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q", kRook + kQueen - kPawn));
}

TEST_CASE("SEE of quiet moves and castling", "[see]") {
    // A quiet move to a safe square exchanges nothing.
    CHECK(see_equals("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "a1a5", 0));
    CHECK(see_equals("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1", 0));
    // Moving the queen where a pawn attacks it loses it.
    CHECK(see_equals("4k3/8/3p4/8/8/8/1Q6/4K3 w - - 0 1", "b2e5", -kQueen));
}
