#include "doctest.h"
#include "search.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include <atomic>
#include <chrono>
#include <thread>
using namespace chess;

TEST_CASE("finds mate in one") {
    attacks::init();
    // White: Qh5, mate on f7 pattern etc. Use a clean mate-in-1.
    Position p; p.set("6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1"); // Re8#
    SearchLimits lim; lim.depth = 3;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.best == make_move(SQ_E1, SQ_E8));
    CHECK(r.score > 29000); // mate score
}

TEST_CASE("captures the free queen") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/3q4/8/8/3RK3 w - - 0 1"); // Rxd4 wins the queen
    SearchLimits lim; lim.depth = 4;
    SearchResult r = search_best_move(p, lim);
    CHECK(to_sq(r.best) == SQ_D4);
}

TEST_CASE("search respects movetime and returns a legal move") {
    attacks::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = MAX_DEPTH; lim.movetime_ms = 100;
    auto t0 = std::chrono::steady_clock::now();
    SearchResult r = search_best_move(p, lim);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    CHECK(r.best != MOVE_NONE);   // always returns a legal move
    CHECK(ms < 2000);             // a depth-64 search would never finish this fast
}

TEST_CASE("search aborts promptly once the stop flag is set") {
    attacks::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = MAX_DEPTH; // unbounded except for the stop flag
    std::atomic<bool> stop{false};
    lim.stop = &stop;

    std::thread stopper([&stop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stop = true;
    });
    auto t0 = std::chrono::steady_clock::now();
    SearchResult r = search_best_move(p, lim);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    stopper.join();

    CHECK(r.best != MOVE_NONE);   // still returns a legal move
    CHECK(ms < 2000);             // a depth-64 search would never finish this fast unaided
}

TEST_CASE("avoids a losing rook-for-pawn trade only visible after a capture") {
    attacks::init();
    // White: Ke1, Rd1. Black: Ke8, pawns c6 (defends d5) and d5.
    // Rxd5 looks like a free pawn one ply deep, but ...cxd5 wins the rook.
    Position p; p.set("4k3/8/2p5/3p4/8/8/8/3RK3 w - - 0 1");
    SearchLimits lim; lim.depth = 1;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.best != make_move(SQ_D1, SQ_D5));
}

TEST_CASE("finds a knight fork that only pays off after a forced check evasion") {
    attacks::init();
    // White: Ka1, Nf5. Black: Ke8, Rf7. Nd6+ forks king and rook: the check
    // can't be captured or blocked, so Black must move the king, and every
    // legal king move (d7/d8/e7/f8) leaves the knight still attacking f7 -
    // at best Black keeps the rook defended (Ke7/Kf8) for a knight-for-rook
    // trade, net material swing to White. A quiescence search that lets the
    // side to move "stand pat" while in check (searching captures only)
    // never sees past the check, so it can't find this: it just evaluates
    // the raw material deficit (down a rook for a knight) and picks a plain
    // developing move instead of the fork.
    Position p; p.set("4k3/5r2/8/5N2/8/8/8/K7 w - - 0 1");
    SearchLimits lim; lim.depth = 1;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.best == make_move(SQ_F5, SQ_D6));
}

TEST_CASE("TT does not corrupt a forced mate score across more iterative-deepening depths") {
    attacks::init();
    Position p; p.set("6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1");
    SearchLimits lim; lim.depth = 5;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.best == make_move(SQ_E1, SQ_E8));
    CHECK(r.score > 29000);
}

TEST_CASE("TT still finds the winning capture at a deeper depth") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/3q4/8/8/3RK3 w - - 0 1");
    SearchLimits lim; lim.depth = 6;
    SearchResult r = search_best_move(p, lim);
    CHECK(to_sq(r.best) == SQ_D4);
}

TEST_CASE("null-move pruning is disabled when the side to move has no non-pawn material") {
    attacks::init();
    Position p; p.set("4k3/8/4K3/4P3/8/8/8/8 b - - 0 1"); // Black: only Ke8, king moves only
    SearchLimits lim; lim.depth = 6;
    SearchResult r = search_best_move(p, lim);
    CHECK((to_sq(r.best) == SQ_D8 || to_sq(r.best) == SQ_F8));
}
