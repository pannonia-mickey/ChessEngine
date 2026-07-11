#include "doctest.h"
#include "perft.hpp"
#include "position.hpp"
#include "attacks.hpp"
using namespace chess;

static std::uint64_t run(const char* fen, int d) {
    Position p; p.set(fen);
    return perft(p, d);
}

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
