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

TEST_CASE("search claims a draw when a losing position has already repeated twice") {
    attacks::init();
    zobrist::init();
    // White Ke1 is boxed in by three black rooks (d-file, f-file, rank 3):
    // e1e2 is White's only legal move, every time. Black shuffles its king
    // harmlessly between h8 and g8 in between.
    Position p; p.set("3r1r1k/8/8/8/8/r7/8/4K3 w - - 0 1");
    std::vector<zobrist::Key> history{p.key()};
    StateInfo st;
    Move shuffle[4] = {
        make_move(SQ_E1, SQ_E2), make_move(SQ_H8, SQ_G8),
        make_move(SQ_E2, SQ_E1), make_move(SQ_G8, SQ_H8),
    };
    for (int rep = 0; rep < 2; ++rep) {
        for (Move m : shuffle) {
            p.do_move(m, st);
            history.push_back(p.key());
        }
    }
    // The starting position has now genuinely occurred twice for real
    // (before and after one full shuffle cycle). White's only legal move
    // replays the cycle a third time.
    SearchLimits lim; lim.depth = 1; lim.history = history;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.best == make_move(SQ_E1, SQ_E2));
    CHECK(r.score == 0);
}

TEST_CASE("search avoids repeating a winning position when a better move exists") {
    attacks::init();
    zobrist::init();
    // Same shuffle idea, but White (Ke1, Qd1) is the one up material this
    // time, against a bare black king that has to shuffle e8/f8 for lack of
    // anything else to do.
    Position p; p.set("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    std::vector<zobrist::Key> history{p.key()};
    StateInfo st;
    Move shuffle[4] = {
        make_move(SQ_E1, SQ_E2), make_move(SQ_E8, SQ_F8),
        make_move(SQ_E2, SQ_E1), make_move(SQ_F8, SQ_E8),
    };
    for (int rep = 0; rep < 2; ++rep) {
        for (Move m : shuffle) {
            p.do_move(m, st);
            history.push_back(p.key());
        }
    }
    // e1e2 would complete a third repetition (score 0, since it already
    // happened for real twice); every other legal move keeps the extra
    // queen instead, so a repetition-aware search must prefer one of those.
    SearchLimits lim; lim.depth = 1; lim.history = history;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.best != make_move(SQ_E1, SQ_E2));
    CHECK(r.score > 0);
}

TEST_CASE("fifty-move rule scores a draw once the halfmove clock reaches 100") {
    attacks::init();
    zobrist::init();
    Position p; p.set("4k3/8/8/8/8/8/8/3QK3 w - - 99 1");
    SearchLimits lim; lim.depth = 1;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.score == 0);
}

TEST_CASE("fifty-move rule does not trigger one ply early") {
    attacks::init();
    zobrist::init();
    Position p; p.set("4k3/8/8/8/8/8/8/3QK3 w - - 98 1");
    SearchLimits lim; lim.depth = 1;
    SearchResult r = search_best_move(p, lim);
    CHECK(r.score > 0);
}
