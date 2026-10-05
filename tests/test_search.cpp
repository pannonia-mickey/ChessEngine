#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "search.hpp"
#include "test_helpers.hpp"

using namespace chess;

namespace {

SearchResult search_to_depth(Position& pos, int depth) {
    SearchLimits limits;
    limits.depth = depth;
    TranspositionTable tt(1);
    return search(pos, limits, tt);
}

std::string best_move(std::string_view fen, int depth) {
    Position pos = test::position_from(fen);
    return search_to_depth(pos, depth).best_move.to_uci();
}

}  // namespace

TEST_CASE("search finds a mate in one", "[search]") {
    Position pos = test::position_from("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
    const SearchResult result = search_to_depth(pos, 3);
    CHECK(result.best_move.to_uci() == "d1d8");
    CHECK(result.score == kMateScore - 1);
    CHECK(mate_in_moves(result.score) == 1);
}

TEST_CASE("search finds a mate in two", "[search]") {
    // 1. Rd8+ Rxd8 2. Rxd8#
    Position pos = test::position_from("r5k1/5ppp/8/8/8/8/3R1PPP/3R2K1 w - - 0 1");
    const SearchResult result = search_to_depth(pos, 4);
    CHECK(result.best_move.to_uci() == "d2d8");
    CHECK(mate_in_moves(result.score) == 2);
}

TEST_CASE("search sees being mated", "[search]") {
    Position pos = test::position_from("3R2k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1");
    // Black is checkmated: no legal move, so no best move and a mated score.
    const SearchResult result = search_to_depth(pos, 2);
    CHECK(result.best_move.is_null());
    CHECK(result.score == -kMateScore);
}

TEST_CASE("search captures a hanging piece and avoids losing its own", "[search]") {
    // The black queen on d5 is unprotected.
    CHECK(best_move("4k3/8/8/3q4/8/2N5/8/4K3 w - - 0 1", 3) == "c3d5");
    // The attacked queen is saved by the pawn capture, not lost to the defended pawn.
    CHECK(best_move("4k3/8/4n3/2p5/1Q1P4/8/8/4K3 w - - 0 1", 3) == "d4c5");
}

TEST_CASE("a stalemated side has no move and a draw score", "[search]") {
    Position pos = test::position_from("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    const SearchResult result = search_to_depth(pos, 3);
    CHECK(result.best_move.is_null());
    CHECK(result.score == kDrawScore);
}

TEST_CASE("search restores the position and is deterministic", "[search]") {
    Position pos =
        test::position_from("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const std::string fen = pos.fen();
    const SearchResult first = search_to_depth(pos, 4);
    CHECK(pos.fen() == fen);
    CHECK(pos.key() == pos.compute_key());
    const SearchResult second = search_to_depth(pos, 4);
    CHECK(first.nodes == second.nodes);
    CHECK(first.best_move == second.best_move);
    CHECK(first.depth == 4);
}

TEST_CASE("search reports every completed iteration", "[search]") {
    Position pos;
    SearchLimits limits;
    limits.depth = 4;
    std::vector<int> depths;
    TranspositionTable tt(1);
    const SearchResult result = search(pos, limits, tt, {}, [&](const SearchInfo& info) {
        depths.push_back(info.depth);
        CHECK_FALSE(info.pv.empty());
    });
    CHECK(depths == std::vector<int>{1, 2, 3, 4});
    CHECK_FALSE(result.best_move.is_null());
}

TEST_CASE("search respects node limits and stop requests", "[search]") {
    Position pos;
    SearchLimits limits;
    limits.nodes = 5000;
    TranspositionTable tt(1);
    const SearchResult limited = search(pos, limits, tt);
    CHECK_FALSE(limited.best_move.is_null());
    CHECK(limited.nodes <= 5000);

    // A stop requested before the search starts still completes the first iteration.
    const std::stop_source source;
    source.request_stop();
    const SearchResult stopped = search(pos, SearchLimits{}, tt, source.get_token());
    CHECK(stopped.depth == 1);
    CHECK_FALSE(stopped.best_move.is_null());
}

TEST_CASE("search leaves a reserve on a low clock even with a large increment", "[search]") {
    using namespace std::chrono_literals;
    Position pos;
    SearchLimits limits;
    // The increment alone would justify spending the whole clock on this move.
    limits.time[White] = 400ms;
    limits.increment[White] = 2000ms;
    TranspositionTable tt(1);
    const auto start = std::chrono::steady_clock::now();
    const SearchResult result = search(pos, limits, tt);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_FALSE(result.best_move.is_null());
    // At most 75% of the 370 ms left after the move overhead, plus some slack for polling.
    CHECK(elapsed < 340ms);
}

TEST_CASE("search scores dead positions and the fifty-move rule as draws", "[search]") {
    // A knight cannot mate a bare king.
    Position knight = test::position_from("8/8/8/4k3/8/8/8/3NK3 w - - 0 1");
    CHECK(search_to_depth(knight, 3).score == kDrawScore);

    // A queen up, but every quiet move completes fifty moves without a capture or pawn move.
    Position fifty = test::position_from("4k3/8/8/8/8/8/8/3QK3 w - - 99 80");
    CHECK(search_to_depth(fifty, 3).score == kDrawScore);
    Position fresh = test::position_from("4k3/8/8/8/8/8/8/3QK3 w - - 0 80");
    CHECK(search_to_depth(fresh, 3).score > 800);
}

TEST_CASE("search does not take a single repetition of the game for a draw", "[search]") {
    // A queen down, Black could go back to a position of the game with Ke8. That repeats it only
    // once, which is not a draw, so the search must still see the lost position.
    Position pos = test::position_from("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    for (const auto* const move : {"e1f2", "e8e7", "f2e1"}) {
        pos.make_move(test::legal_move(pos, move));
    }
    CHECK(search_to_depth(pos, 4).score < -500);
}
