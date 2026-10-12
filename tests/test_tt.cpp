#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "move.hpp"
#include "search.hpp"
#include "test_helpers.hpp"
#include "tt.hpp"

using namespace chess;

namespace {

constexpr Move kE2E4{E2, E4};
constexpr Move kD2D4{D2, D4};

// The entry stored for `key`, which must exist.
TtEntry probe(const TranspositionTable& tt, Key key) {
    const auto entry = tt.probe(key);
    REQUIRE(entry.has_value());
    return entry.value_or(TtEntry{});
}

}  // namespace

TEST_CASE("the table returns what was stored and nothing for unknown keys", "[tt]") {
    TranspositionTable tt(1);
    constexpr Key kKey = 0x1234'5678'9ABC'DEF0ULL;
    CHECK_FALSE(tt.probe(kKey).has_value());

    tt.store(kKey, kE2E4, -42, 0, 7, Bound::Lower);
    const TtEntry entry = probe(tt, kKey);
    CHECK(entry.move == kE2E4);
    CHECK(entry.score == -42);
    CHECK(entry.depth == 7);
    CHECK(entry.bound == Bound::Lower);

    // Same slot, different position: the key check rejects it.
    CHECK_FALSE(tt.probe(kKey ^ (std::uint64_t{1} << 63U)).has_value());

    tt.clear();
    CHECK_FALSE(tt.probe(kKey).has_value());
}

TEST_CASE("the table keeps deeper results of the current search", "[tt]") {
    TranspositionTable tt(1);
    constexpr Key kKey = 42;
    tt.new_search();
    tt.store(kKey, kE2E4, 10, 0, 8, Bound::Lower);

    // A shallower bound does not replace it, but an exact score does.
    tt.store(kKey, kD2D4, 20, 0, 3, Bound::Upper);
    CHECK(probe(tt, kKey).depth == 8);
    tt.store(kKey, kD2D4, 30, 0, 3, Bound::Exact);
    CHECK(probe(tt, kKey).score == 30);

    // A fail-low store without a move keeps the known best move.
    tt.store(kKey, Move::null(), 5, 0, 4, Bound::Upper);
    CHECK(probe(tt, kKey).move == kD2D4);

    // Results of an earlier search are always replaced.
    tt.store(kKey, kE2E4, 10, 0, 9, Bound::Lower);
    tt.new_search();
    tt.store(kKey, kD2D4, 0, 0, 1, Bound::Upper);
    CHECK(probe(tt, kKey).depth == 1);
}

TEST_CASE("the table keeps the static evaluation and no evaluation", "[tt]") {
    TranspositionTable tt(1);
    tt.store(1, kE2E4, 10, -123, 3, Bound::Upper);
    tt.store(2, kE2E4, 10, kNoEval, 3, Bound::Upper);
    CHECK(probe(tt, 1).eval == -123);
    CHECK(probe(tt, 2).eval == kNoEval);
}

TEST_CASE("positions sharing a bucket are all kept until the bucket is full", "[tt]") {
    TranspositionTable tt(1);
    // Keys differing only above the index bits select the same bucket.
    const auto key = [](std::uint64_t n) { return Key{(n + 1) << 40U}; };
    for (std::uint64_t n = 0; n < TranspositionTable::kBucketSize; ++n) {
        tt.store(key(n), kE2E4, static_cast<Score>(n), 0, static_cast<int>(n) + 1, Bound::Lower);
    }
    for (std::uint64_t n = 0; n < TranspositionTable::kBucketSize; ++n) {
        CHECK(probe(tt, key(n)).score == static_cast<Score>(n));
    }

    // A new position replaces the shallowest entry.
    tt.store(key(100), kD2D4, 7, 0, 5, Bound::Exact);
    CHECK(probe(tt, key(100)).score == 7);
    CHECK_FALSE(tt.probe(key(0)).has_value());
    CHECK(tt.probe(key(1)).has_value());

    // After a new search, the older entries count as shallower than they are.
    tt.new_search();
    tt.store(key(101), kD2D4, 8, 0, 1, Bound::Exact);
    CHECK(probe(tt, key(101)).score == 8);
    CHECK(probe(tt, key(100)).score == 7);
    CHECK(probe(tt, key(TranspositionTable::kBucketSize - 1)).depth ==
          static_cast<int>(TranspositionTable::kBucketSize));
}

TEST_CASE("a quiescence result does not replace a deeper one of the current search", "[tt]") {
    TranspositionTable tt(1);
    constexpr Key kKey = 7;
    tt.new_search();
    tt.store(kKey, kE2E4, 10, 0, 6, Bound::Lower);
    tt.store(kKey, kD2D4, 20, 0, 0, Bound::Exact);
    CHECK(probe(tt, kKey).depth == 6);
}

TEST_CASE("the table size is a power of two that fits the requested memory", "[tt]") {
    TranspositionTable tt(1);
    const std::size_t one = tt.slot_count();
    CHECK((one & (one - 1)) == 0);
    tt.resize(4);
    CHECK(tt.slot_count() == one * 4);
    CHECK(tt.hashfull() == 0);
}

TEST_CASE("search fills the table and reuses it in the next search", "[tt][search]") {
    Position pos;
    SearchLimits limits;
    limits.depth = 5;
    TranspositionTable tt(1);
    const SearchResult first = search(pos, limits, tt);
    const TtEntry root = probe(tt, pos.key());
    CHECK(root.move == first.best_move);
    CHECK(root.bound == Bound::Exact);

    const SearchResult second = search(pos, limits, tt);
    CHECK(second.nodes < first.nodes);
    CHECK(second.best_move == first.best_move);
}

TEST_CASE("mate scores read back from the table count from the root", "[tt][search]") {
    // 1. Rd8+ Rxd8 2. Rxd8#: searching again with a warm table must still report mate in two.
    Position pos = test::position_from("r5k1/5ppp/8/8/8/8/3R1PPP/3R2K1 w - - 0 1");
    SearchLimits limits;
    limits.depth = 6;
    TranspositionTable tt(1);
    const SearchResult cold = search(pos, limits, tt);
    const SearchResult warm = search(pos, limits, tt);
    CHECK(cold.score == kMateScore - 3);
    CHECK(warm.score == kMateScore - 3);
    CHECK(warm.best_move.to_uci() == "d2d8");
}
