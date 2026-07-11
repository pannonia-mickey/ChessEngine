#include "doctest.h"
#include "position.hpp"
#include "attacks.hpp"
#include "zobrist.hpp"
#include "move.hpp"
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
