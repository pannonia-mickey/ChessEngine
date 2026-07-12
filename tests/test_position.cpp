#include "doctest.h"
#include "position.hpp"
#include "attacks.hpp"
#include "move.hpp"
using namespace chess;

static const char* START =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

TEST_CASE("FEN parse of start position") {
    attacks::init();
    Position p; p.set(START);
    CHECK(p.side_to_move() == WHITE);
    CHECK(p.piece_on(SQ_E1) == W_KING);
    CHECK(p.piece_on(SQ_E8) == B_KING);
    CHECK(popcount(p.pieces(WHITE, PAWN)) == 8);
    CHECK(popcount(p.occupied()) == 32);
    CHECK(p.castling_rights() == (1 | 2 | 4 | 8));
    CHECK(p.ep_square() == SQ_NONE);
    CHECK(p.king_square(WHITE) == SQ_E1);
}

TEST_CASE("FEN round-trips") {
    attacks::init();
    Position p; p.set(START);
    CHECK(p.fen() == START);
}

TEST_CASE("attack detection") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
    CHECK(p.square_attacked_by(SQ_A8, WHITE)); // rook on a1 hits a8
    CHECK(p.square_attacked_by(SQ_E8, WHITE) == false);
}

TEST_CASE("do/undo restores position exactly") {
    attacks::init();
    Position p; p.set(START);
    std::string before = p.fen();
    StateInfo st;
    p.do_move(make_move(SQ_E2, SQ_E4), st);
    CHECK(p.piece_on(SQ_E4) == W_PAWN);
    CHECK(p.piece_on(SQ_E2) == NO_PIECE);
    CHECK(p.side_to_move() == BLACK);
    CHECK(p.ep_square() == SQ_E3);
    p.undo_move(make_move(SQ_E2, SQ_E4), st);
    CHECK(p.fen() == before);
}

TEST_CASE("castling moves king and rook, undo restores") {
    attacks::init();
    Position p; p.set("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    std::string before = p.fen();
    StateInfo st;
    p.do_move(make_move(SQ_E1, SQ_G1, CASTLING), st);
    CHECK(p.piece_on(SQ_G1) == W_KING);
    CHECK(p.piece_on(SQ_F1) == W_ROOK);
    p.undo_move(make_move(SQ_E1, SQ_G1, CASTLING), st);
    CHECK(p.fen() == before);
}

TEST_CASE("en passant capture moves pawn and removes captured pawn, undo restores") {
    attacks::init();
    static const char* EP_FEN = "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";
    Position p; p.set(EP_FEN);
    std::string before = p.fen();
    StateInfo st;
    p.do_move(make_move(SQ_E5, SQ_D6, EN_PASSANT), st);
    CHECK(p.piece_on(SQ_D6) == W_PAWN);
    CHECK(p.piece_on(SQ_E5) == NO_PIECE);
    CHECK(p.piece_on(SQ_D5) == NO_PIECE);
    p.undo_move(make_move(SQ_E5, SQ_D6, EN_PASSANT), st);
    CHECK(p.fen() == before);
}

TEST_CASE("promotion with capture replaces pawn with piece, undo restores") {
    attacks::init();
    static const char* PROMO_FEN = "4k2r/6P1/8/8/8/8/8/4K3 w - - 0 1";
    Position p; p.set(PROMO_FEN);
    std::string before = p.fen();
    StateInfo st;
    p.do_move(make_move(SQ_G7, SQ_H8, PROMOTION, QUEEN), st);
    CHECK(p.piece_on(SQ_H8) == W_QUEEN);
    CHECK(p.piece_on(SQ_G7) == NO_PIECE);
    p.undo_move(make_move(SQ_G7, SQ_H8, PROMOTION, QUEEN), st);
    CHECK(p.fen() == before);
}

TEST_CASE("castling rights are dropped when the rook they refer to isn't there") {
    attacks::init();
    // Claims all four rights but no rooks exist on the board at all.
    Position p; p.set("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1");
    CHECK(p.castling_rights() == 0);
}

TEST_CASE("en passant square is dropped when there's no pawn to justify it") {
    attacks::init();
    // Claims an e6 ep square (would require a black pawn on e5) on an
    // otherwise-empty board.
    Position p; p.set("4k3/8/8/8/8/8/8/4K3 w - e6 0 1");
    CHECK(p.ep_square() == SQ_NONE);
}

TEST_CASE("FEN with a pawn on the back rank is rejected") {
    attacks::init();
    Position p;
    CHECK(p.set("P3k3/8/8/8/8/8/8/4K3 w - - 0 1") == false);
    CHECK(p.set("4k3/8/8/8/8/8/8/p3K3 w - - 0 1") == false);
}

TEST_CASE("FEN leaving the side not to move in check is rejected") {
    attacks::init();
    // Black rook on e4 checks the white king on e1 along the e-file.
    // With Black to move, White (not to move) being in check is illegal:
    // White's own last move would have had to leave its king in check.
    Position p;
    CHECK(p.set("4k3/8/8/8/4r3/8/8/4K3 b - - 0 1") == false);
}

TEST_CASE("FEN leaving the side to move itself in check is accepted") {
    attacks::init();
    // Same board, but White to move: White's own king being in check is
    // completely legal (White is simply in check and must respond to it).
    Position p;
    CHECK(p.set("4k3/8/8/8/4r3/8/8/4K3 w - - 0 1") == true);
    CHECK(p.side_to_move() == WHITE);
}

TEST_CASE("FEN missing the opponent's king entirely is still accepted") {
    attacks::init();
    // No black king on the board at all. king_square() assumes exactly one
    // king per side, so the opponent-in-check validation must not attempt
    // to evaluate this case (some tests intentionally build single-sided
    // boards, e.g. tests/test_movegen.cpp's pinned-piece test).
    Position p;
    CHECK(p.set("8/8/8/8/4r3/8/8/4K3 w - - 0 1") == true);
}

TEST_CASE("capturing rook on home square clears that side's castling right, undo restores") {
    attacks::init();
    static const char* ROOK_FEN = "4k3/8/8/8/8/2b5/8/R3K2R b KQ - 0 1";
    Position p; p.set(ROOK_FEN);
    std::string before = p.fen();
    StateInfo st;
    p.do_move(make_move(SQ_C3, SQ_A1), st);
    CHECK((p.castling_rights() & WHITE_OOO) == 0);
    CHECK((p.castling_rights() & WHITE_OO) != 0);
    p.undo_move(make_move(SQ_C3, SQ_A1), st);
    CHECK(p.fen() == before);
}

TEST_CASE("null move flips side to move, clears ep, and undoes exactly") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    StateInfo st;
    p.do_move(make_move(SQ_E2, SQ_E4), st); // sets the ep square e3, black to move
    std::string before = p.fen();
    zobrist::Key key_before = p.key();

    NullMoveState nst;
    p.do_null_move(nst);
    CHECK(p.side_to_move() == WHITE);
    CHECK(p.ep_square() == SQ_NONE);

    p.undo_null_move(nst);
    CHECK(p.fen() == before);
    CHECK(p.key() == key_before);
}

TEST_CASE("halfmove accessor reflects the FEN and updates through do/undo") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/8/8/8/4K3 w - - 5 10");
    CHECK(p.halfmove() == 5);
    StateInfo st;
    p.do_move(make_move(SQ_E1, SQ_E2), st);
    CHECK(p.halfmove() == 6);
    p.undo_move(make_move(SQ_E1, SQ_E2), st);
    CHECK(p.halfmove() == 5);
}
