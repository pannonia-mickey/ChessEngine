#include "doctest.h"
#include "movegen.hpp"
#include "position.hpp"
#include "attacks.hpp"
using namespace chess;

static int legal_count(const char* fen) {
    Position p; p.set(fen);
    MoveList l; generate_legal(p, l);
    return l.size;
}

TEST_CASE("legal move counts") {
    attacks::init();
    CHECK(legal_count("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") == 20);
    // King in check must respond; only a few legal moves.
    CHECK(legal_count("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1") > 0);
}

TEST_CASE("pinned piece cannot expose the king") {
    attacks::init();
    // White rook on e2 is pinned to the king on e1 by the black rook on e8:
    // it may slide along the e-file but not step off it.
    Position p; p.set("4r3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    MoveList l; generate_legal(p, l);
    bool moved_off_file = false;
    bool moved_along_file = false;
    for (Move m : l) {
        if (from_sq(m) != SQ_E2) continue;
        if (file_of(to_sq(m)) != file_of(SQ_E2)) moved_off_file = true;
        if (to_sq(m) == SQ_E3) moved_along_file = true;
    }
    CHECK_FALSE(moved_off_file);
    CHECK(moved_along_file);
}

TEST_CASE("MoveList::add caps at capacity instead of overflowing") {
    MoveList l;
    for (int i = 0; i < 300; ++i) l.add(make_move(SQ_A1, SQ_A2));
    CHECK(l.size == 256);
}
