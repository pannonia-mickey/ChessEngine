#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <string>

#include "movegen.hpp"
#include "position.hpp"
#include "test_helpers.hpp"

using namespace chess;

namespace {

// Plays every legal line to the given depth and checks that the incremental Zobrist key matches
// a recomputation and that unmake_move restores the exact position.
// NOLINTNEXTLINE(misc-no-recursion)
void check_make_unmake(Position& pos, int depth) {
    if (depth == 0) {
        return;
    }
    const std::string fen = pos.fen();
    const Key key = pos.key();
    for (const Move move : generate_legal_moves(pos)) {
        pos.make_move(move);
        REQUIRE(pos.key() == pos.compute_key());
        check_make_unmake(pos, depth - 1);
        pos.unmake_move();
        REQUIRE(pos.fen() == fen);
        REQUIRE(pos.key() == key);
    }
}

}  // namespace

TEST_CASE("default position is the start position", "[position]") {
    const Position pos;
    CHECK(pos.fen() == Position::kStartFen);
    CHECK(pos.side_to_move() == White);
    CHECK(pos.castling_rights() == kAllCastling);
    CHECK(pos.en_passant_square() == NoSquare);
    CHECK(pos.piece_on(E1) == WhiteKing);
    CHECK(pos.piece_on(D8) == BlackQueen);
    CHECK(pos.piece_on(E4) == NoPiece);
    CHECK(popcount(pos.pieces()) == 32);
    CHECK(pos.key() == pos.compute_key());
}

TEST_CASE("FEN round trip", "[position][fen]") {
    const std::string fen =
        GENERATE(std::string(Position::kStartFen),
                 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                 "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
                 "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
                 "4k3/8/8/8/8/8/8/4K3 b - - 99 150");
    const Position pos = test::position_from(fen);
    CHECK(pos.fen() == fen);
    CHECK(pos.key() == pos.compute_key());
}

TEST_CASE("FEN normalization", "[position][fen]") {
    SECTION("move counters are optional") {
        const Position pos = test::position_from("4k3/8/8/8/8/8/8/4K3 w - -");
        CHECK(pos.fen() == "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    }
    SECTION("an en passant square nobody can capture on is dropped") {
        const Position pos =
            test::position_from("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
        CHECK(pos.en_passant_square() == NoSquare);
    }
    SECTION("castling rights without the king or rook in place are dropped") {
        const Position pos = test::position_from("4k3/8/8/8/8/8/8/R3K3 w KQkq - 0 1");
        CHECK(pos.castling_rights() == kWhiteQueenSide);
    }
}

TEST_CASE("invalid FENs are rejected", "[position][fen]") {
    const std::string fen = GENERATE("", "8/8/8/8/8/8/8/8 w - - 0 1",   // no kings
                                     "4k3/8/8/8/8/8/8/4K3 x - - 0 1",   // bad side to move
                                     "4k3/8/8/8/8/8/8/4K2 w - - 0 1",   // short rank
                                     "4k3/8/8/8/8/8/8/4K4 w - - 0 1",   // long rank
                                     "4k3/8/8/8/8/8/8 w - - 0 1",       // missing rank
                                     "4k3/8/8/8/8/8/8/4KX2 w - - 0 1",  // bad piece
                                     "4k3/8/8/8/8/8/8/4K3 w X - 0 1",   // bad castling
                                     "4k3/8/8/8/8/8/8/4K3 w - e4 0 1",  // bad en passant rank
                                     "4k3/8/8/8/8/8/8/4K3 w - - x 1",   // bad halfmove clock
                                     "4k3/8/8/8/8/8/8/4K3 w - - 0 0",   // bad fullmove number
                                     "P3k3/8/8/8/8/8/8/4K3 w - - 0 1",  // pawn on the back rank
                                     "4k3/8/8/8/8/8/8/4QK2 w - - 0 1",  // side not to move in check
                                     "4k3/8/8/8/8/8/8/3KK3 w - - 0 1");  // two white kings
    CHECK_FALSE(Position::from_fen(fen).has_value());
}

TEST_CASE("make and unmake keep the position and key consistent", "[position][zobrist]") {
    const std::string fen =
        GENERATE(std::string(Position::kStartFen),
                 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
                 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    Position pos = test::position_from(fen);
    check_make_unmake(pos, 3);
}

TEST_CASE("transpositions reach the same key", "[position][zobrist]") {
    Position a;
    Position b;
    for (const auto* move : {"g1f3", "g8f6", "b1c3", "b8c6"}) {
        a.make_move(test::legal_move(a, move));
    }
    for (const auto* move : {"b1c3", "b8c6", "g1f3", "g8f6"}) {
        b.make_move(test::legal_move(b, move));
    }
    CHECK(a.key() == b.key());
    CHECK(a.key() != Position().key());
}

TEST_CASE("special moves", "[position][movegen]") {
    SECTION("en passant removes the captured pawn") {
        Position pos = test::position_from("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        pos.make_move(test::legal_move(pos, "e5d6"));
        CHECK(pos.fen() == "4k3/8/3P4/8/8/8/8/4K3 b - - 0 2");
    }
    SECTION("castling moves the rook and clears the rights") {
        Position pos = test::position_from("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        pos.make_move(test::legal_move(pos, "e1g1"));
        CHECK(pos.fen() == "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
        pos.make_move(test::legal_move(pos, "e8c8"));
        CHECK(pos.fen() == "2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2");
    }
    SECTION("capturing a rook removes the opponent's castling right") {
        Position pos = test::position_from("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        pos.make_move(test::legal_move(pos, "a1a8"));
        CHECK(pos.fen() == "R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1");
    }
    SECTION("promotion with capture") {
        Position pos = test::position_from("1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        CHECK_FALSE(parse_uci_move(pos, "a7b8").has_value());
        pos.make_move(test::legal_move(pos, "a7b8n"));
        CHECK(pos.fen() == "1N2k3/8/8/8/8/8/8/4K3 b - - 0 1");
        pos.unmake_move();
        CHECK(pos.fen() == "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    }
}

TEST_CASE("check detection", "[position]") {
    const Position pos = test::position_from("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    CHECK(pos.in_check());
    CHECK(pos.checkers() == square_bb(E2));
    CHECK_FALSE(Position().in_check());
}

TEST_CASE("repetitions are detected since the last irreversible move", "[position]") {
    Position pos;
    for (const auto* const move : {"g1f3", "g8f6", "f3g1"}) {
        pos.make_move(test::legal_move(pos, move));
        CHECK_FALSE(pos.is_repetition());
    }
    pos.make_move(test::legal_move(pos, "f6g8"));
    CHECK(pos.is_repetition());

    // A pawn move makes the earlier positions unreachable.
    pos.make_move(test::legal_move(pos, "e2e4"));
    for (const auto* const move : {"g8f6", "g1f3", "f6g8", "f3g1"}) {
        pos.make_move(test::legal_move(pos, move));
    }
    CHECK(pos.is_repetition());
    pos.unmake_move();
    CHECK_FALSE(pos.is_repetition());
}

TEST_CASE("a null move passes the turn and is taken back exactly", "[position][zobrist]") {
    // Black can capture en passant, so the square is part of the key and must be cleared.
    Position pos = test::position_from("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
    const std::string fen = pos.fen();
    const Key key = pos.key();
    pos.make_null_move();
    CHECK(pos.side_to_move() == White);
    CHECK(pos.en_passant_square() == NoSquare);
    CHECK(pos.key() == pos.compute_key());
    pos.unmake_null_move();
    CHECK(pos.fen() == fen);
    CHECK(pos.key() == key);
}

TEST_CASE("repetitions are not detected across a null move", "[position]") {
    Position pos = test::position_from("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
    // The white king triangulates, then Black passes: the start position with White to move.
    for (const auto* const move : {"e1d1", "e8d8", "d1d2", "d8e8", "d2e1"}) {
        pos.make_move(test::legal_move(pos, move));
    }
    pos.make_null_move();
    REQUIRE(pos.key() == test::position_from("4k3/8/8/8/8/8/8/R3K3 w - - 0 1").key());
    CHECK_FALSE(pos.is_repetition());
}
