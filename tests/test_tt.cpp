#include "doctest.h"
#include "tt.hpp"
#include "move.hpp"
using namespace chess;

TEST_CASE("probe on an empty table returns nullptr") {
    TranspositionTable tt(1);
    CHECK(tt.probe(12345) == nullptr);
}

TEST_CASE("store then probe returns the stored entry") {
    TranspositionTable tt(1);
    tt.store(42, 5, 123, TT_EXACT, make_move(SQ_E2, SQ_E4));
    const TTEntry* e = tt.probe(42);
    REQUIRE(e != nullptr);
    CHECK(e->depth == 5);
    CHECK(e->score == 123);
    CHECK(e->bound == TT_EXACT);
    CHECK(e->best == make_move(SQ_E2, SQ_E4));
}

TEST_CASE("probing an unstored key returns nullptr") {
    TranspositionTable tt(1);
    tt.store(42, 5, 123, TT_EXACT, MOVE_NONE);
    CHECK(tt.probe(99999) == nullptr);
}

TEST_CASE("clear() empties all entries") {
    TranspositionTable tt(1);
    tt.store(42, 5, 123, TT_EXACT, MOVE_NONE);
    tt.clear();
    CHECK(tt.probe(42) == nullptr);
}

TEST_CASE("store overwrites a previous entry at the same key (always-replace)") {
    TranspositionTable tt(1);
    tt.store(42, 3, 100, TT_LOWER, MOVE_NONE);
    tt.store(42, 7, 200, TT_UPPER, make_move(SQ_A2, SQ_A4));
    const TTEntry* e = tt.probe(42);
    REQUIRE(e != nullptr);
    CHECK(e->depth == 7);
    CHECK(e->score == 200);
    CHECK(e->bound == TT_UPPER);
}

TEST_CASE("resize() reinitializes the table, discarding old entries") {
    TranspositionTable tt(1);
    tt.store(42, 5, 123, TT_EXACT, MOVE_NONE);
    tt.resize(2);
    CHECK(tt.probe(42) == nullptr);
}

TEST_CASE("hashfull() is 0 on an empty table") {
    TranspositionTable tt(1);
    CHECK(tt.hashfull() == 0);
}

TEST_CASE("hashfull() reflects stored entries out of 1000 permille") {
    // 1 MB / sizeof(TTEntry) rounded down to a power of two is well over
    // 1000 entries, so keys 0..499 land at distinct indices (index == key,
    // since each key is smaller than the table's mask) within the first
    // 1000 slots hashfull() samples.
    TranspositionTable tt(1);
    for (std::uint64_t k = 0; k < 500; ++k)
        tt.store(k, 1, 0, TT_EXACT, MOVE_NONE);
    CHECK(tt.hashfull() == 500);
}
