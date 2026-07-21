#include "doctest.h"
#include "position.hpp"
#include "attacks.hpp"
#include "zobrist.hpp"
#include "move.hpp"
#include "movegen.hpp"
#include "bitboard.hpp"
using namespace chess;

TEST_CASE("same position always hashes to the same key") {
    attacks::init();
    zobrist::init();
    Position a; a.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Position b; b.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(a.key() == b.key());
}

TEST_CASE("different side to move hashes differently") {
    attacks::init();
    zobrist::init();
    Position a; a.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Position b; b.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
    CHECK(a.key() != b.key());
}

TEST_CASE("do_move/undo_move round-trips the key exactly") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    zobrist::Key before = p.key();
    StateInfo st;
    Move m = make_move(SQ_E2, SQ_E4);
    p.do_move(m, st);
    CHECK(p.key() != before);
    p.undo_move(m, st);
    CHECK(p.key() == before);
}

TEST_CASE("key matches a from-scratch recompute after a pawn double push (ep hash)") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    StateInfo st;
    p.do_move(make_move(SQ_E2, SQ_E4), st);
    zobrist::Key incremental = p.key();

    Position fresh; fresh.set(p.fen());
    CHECK(fresh.key() == incremental);
}

TEST_CASE("key matches a from-scratch recompute after losing castling rights") {
    attacks::init();
    zobrist::init();
    Position p; p.set("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    StateInfo st;
    p.do_move(make_move(SQ_E1, SQ_E2), st); // king move forfeits both White rights
    zobrist::Key incremental = p.key();

    Position fresh; fresh.set(p.fen());
    CHECK(fresh.key() == incremental);
}

TEST_CASE("pawn_key changes on a pawn move but not on a non-pawn move") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    zobrist::Key before = p.pawn_key();
    StateInfo st;

    Move pawn_move = make_move(SQ_E2, SQ_E4);
    p.do_move(pawn_move, st);
    CHECK(p.pawn_key() != before);
    p.undo_move(pawn_move, st);
    CHECK(p.pawn_key() == before);

    Move knight_move = make_move(SQ_G1, SQ_F3);
    p.do_move(knight_move, st);
    CHECK(p.pawn_key() == before); // knight move must not touch pawn_key_
    p.undo_move(knight_move, st);
}

TEST_CASE("pawn_key matches a from-scratch recompute after a pawn capture") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 1");
    StateInfo st;
    p.do_move(make_move(SQ_D4, SQ_E5), st); // pawn captures pawn
    zobrist::Key incremental = p.pawn_key();

    Position fresh; fresh.set(p.fen());
    CHECK(fresh.pawn_key() == incremental);
}

namespace {
zobrist::Key reference_pawn_key(const Position& pos) {
    zobrist::Key k = 0;
    for (int c = 0; c < COLOR_NB; ++c) {
        Bitboard pawns = pos.pieces(Color(c), PAWN);
        while (pawns) {
            Square s = pop_lsb(pawns);
            k ^= zobrist::psq[make_piece(Color(c), PAWN)][s];
        }
    }
    return k;
}

void walk_and_check_pawn_key(Position& pos, int depth) {
    CHECK(pos.pawn_key() == reference_pawn_key(pos));
    if (depth == 0) return;
    MoveList moves;
    generate_legal(pos, moves);
    StateInfo st;
    for (Move m : moves) {
        pos.do_move(m, st);
        walk_and_check_pawn_key(pos, depth - 1);
        pos.undo_move(m, st);
    }
}
} // namespace

TEST_CASE("pawn_key matches a from-scratch reference across a do/undo tree") {
    attacks::init();
    zobrist::init();
    Position p; p.set("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    walk_and_check_pawn_key(p, 3);
}
