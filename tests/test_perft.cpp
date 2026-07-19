#include "doctest.h"
#include "perft.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "movegen.hpp"
using namespace chess;

namespace {

// Reference (slow but obviously-correct) legality filter: do/undo every
// pseudo move and check whether the king survives it. This is the
// pre-optimization generate_legal algorithm, kept here purely as a test
// oracle for the fast checkers/pinned-based generate_legal in movegen.cpp -
// any divergence between the two here is a movegen bug, not a perft bug.
void generate_legal_slow(Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_pseudo(pos, pseudo);
    Color us = pos.side_to_move();
    Color them = Color(us ^ 1);
    for (Move m : pseudo) {
        if (flag_of(m) == CASTLING) {
            Square from = from_sq(m);
            Square to = to_sq(m);
            int step = (to > from) ? 1 : -1;
            bool safe = true;
            for (Square s = from;; s = Square(int(s) + step)) {
                if (pos.square_attacked_by(s, them)) { safe = false; break; }
                if (s == to) break;
            }
            if (!safe) continue;
        }
        StateInfo st;
        pos.do_move(m, st);
        bool legal = !pos.square_attacked_by(pos.king_square(us), them);
        pos.undo_move(m, st);
        if (legal) list.add(m);
    }
}

bool move_lists_equal(const MoveList& a, const MoveList& b) {
    if (a.size != b.size) return false;
    for (int i = 0; i < a.size; ++i)
        if (a.moves[i] != b.moves[i]) return false;
    return true;
}

// Recursively walks the tree of legal moves (per the reference generator),
// asserting at every node that the fast generate_legal produces the exact
// same move set AND order as the reference. A pure node-count match (as
// perft alone would give) can't catch an ordering bug or an
// equal-size-but-wrong-content divergence; comparing the lists directly can.
void check_matches_reference(Position& pos, int depth) {
    MoveList fast; generate_legal(pos, fast);
    MoveList slow; generate_legal_slow(pos, slow);
    CHECK(move_lists_equal(fast, slow));
    if (depth == 0) return;
    StateInfo st;
    for (Move m : slow) {
        pos.do_move(m, st);
        check_matches_reference(pos, depth - 1);
        pos.undo_move(m, st);
    }
}

std::uint64_t run(const char* fen, int d) {
    Position p;
    p.set(fen);
    return perft(p, d);
}

} // namespace

TEST_CASE("perft startpos") {
    attacks::init();
    const char* s = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    CHECK(run(s, 1) == 20);
    CHECK(run(s, 2) == 400);
    CHECK(run(s, 3) == 8902);
    CHECK(run(s, 4) == 197281);
    CHECK(run(s, 5) == 4865609);
}

TEST_CASE("perft kiwipete") {
    attacks::init();
    const char* k = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    CHECK(run(k, 1) == 48);
    CHECK(run(k, 2) == 2039);
    CHECK(run(k, 3) == 97862);
}

TEST_CASE("perft position 3/4/5") {
    attacks::init();
    CHECK(run("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5) == 674624);
    CHECK(run("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3) == 9467);
    CHECK(run("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3) == 62379);
}

// Regression coverage for the fast checkers/pinned-based generate_legal
// (src/movegen.cpp): every legal move list it produces, at every node of a
// several-ply search tree, must match the do/undo reference implementation
// exactly - both the set of moves and their order (the NPS protocol in
// docs/perf/nps-baseline.md requires the search tree to be bit-for-bit
// unchanged, which depends on move order, not just move count). Covers
// castling, pins, and the en passant/discovered-check edge case (baked into
// the "position 3/4/5" FEN below) via tree traversal, plus an explicit
// double-check position that perft's node counts alone wouldn't expose an
// ordering bug in.
TEST_CASE("fast generate_legal matches do/undo reference") {
    attacks::init();
    struct { const char* fen; int depth; } cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3},
        // Double check (rook e8 + knight d3 both attack king e1 at once):
        // only king moves can be legal here.
        {"k3r3/8/8/8/8/3n4/8/4K3 w - - 0 1", 2},
        // Pinned rook (e2, pinned to e1 by the rook on e8): may only slide
        // along the pin ray.
        {"k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1", 3},
    };
    for (auto& c : cases) {
        Position p;
        p.set(c.fen);
        check_matches_reference(p, c.depth);
    }
}
