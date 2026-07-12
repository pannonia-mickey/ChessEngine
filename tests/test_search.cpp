#include "doctest.h"
#include "search.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "tt.hpp"
#include <atomic>
#include <chrono>
#include <thread>
using namespace chess;

TEST_CASE("finds mate in one") {
    attacks::init();
    // White: Qh5, mate on f7 pattern etc. Use a clean mate-in-1.
    Position p; p.set("6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1"); // Re8#
    SearchLimits lim; lim.depth = 3;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_E1, SQ_E8));
    CHECK(r.score > 29000); // mate score
}

TEST_CASE("captures the free queen") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/3q4/8/8/3RK3 w - - 0 1"); // Rxd4 wins the queen
    SearchLimits lim; lim.depth = 4;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(to_sq(r.best) == SQ_D4);
}

TEST_CASE("search respects movetime and returns a legal move") {
    attacks::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = MAX_DEPTH; lim.movetime_ms = 100;
    auto t0 = std::chrono::steady_clock::now();
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
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
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
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
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
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
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_F5, SQ_D6));
}

TEST_CASE("TT does not corrupt a forced mate score across more iterative-deepening depths") {
    attacks::init();
    Position p; p.set("6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1");
    SearchLimits lim; lim.depth = 5;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_E1, SQ_E8));
    CHECK(r.score > 29000);
}

TEST_CASE("TT still finds the winning capture at a deeper depth") {
    attacks::init();
    Position p; p.set("4k3/8/8/8/3q4/8/8/3RK3 w - - 0 1");
    SearchLimits lim; lim.depth = 6;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(to_sq(r.best) == SQ_D4);
}

TEST_CASE("null-move pruning is disabled when the side to move has no non-pawn material") {
    attacks::init();
    Position p; p.set("4k3/8/4K3/4P3/8/8/8/8 b - - 0 1"); // Black: only Ke8, king moves only
    SearchLimits lim; lim.depth = 6;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
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
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
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
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best != make_move(SQ_E1, SQ_E2));
    CHECK(r.score > 0);
}

TEST_CASE("fifty-move rule scores a draw once the halfmove clock reaches 100") {
    attacks::init();
    zobrist::init();
    Position p; p.set("4k3/8/8/8/8/8/8/3QK3 w - - 99 1");
    SearchLimits lim; lim.depth = 1;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.score == 0);
}

TEST_CASE("fifty-move rule does not trigger one ply early") {
    attacks::init();
    zobrist::init();
    Position p; p.set("4k3/8/8/8/8/8/8/3QK3 w - - 98 1");
    SearchLimits lim; lim.depth = 1;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.score > 0);
}

TEST_CASE("fifty-move rule scores a draw when the clock reaches 100 inside quiescence") {
    attacks::init();
    zobrist::init();
    // White is down a bishop and two pawns with no way to recapture, so any
    // quiet move stays clearly worse than a draw - except Nd6-f7+, whose
    // only legal reply (Kh8-g8, forced: g7/h7 are Black's own pawns, Bf8
    // blocks f8, and f7 is now occupied by White's knight) pushes the
    // halfmove clock from 99 to 100 one ply into quiescence's own
    // check-evasion recursion, which is where the draw is actually claimed
    // - negamax's own copy of the check doesn't fire here (halfmove is
    // still 99 when negamax hands off to quiescence).
    Position p; p.set("5b1k/6pp/3N4/8/8/8/8/6K1 w - - 98 1");
    SearchLimits lim; lim.depth = 1;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_D6, SQ_F7));
    CHECK(r.score == 0);
}

TEST_CASE("search finds a draw by perpetual check that only repeats inside its own search tree") {
    attacks::init();
    zobrist::init();
    // White (bare Kg1 + Nd6) is down a bishop, a knight and a pawn against
    // Black Kh8, Bf8, Ng7, Ph7, so any quiet move loses outright - except
    // Nd6-f7+, which starts a perpetual check the knight can keep up forever
    // by oscillating f7<->h6: from h8 in check from Nf7, g7/h7 are Black's
    // own pieces and Bf8 blocks f8, so Kh8-g8 is forced; from g8 in check
    // from Nh6 (which also covers f7, the square it just vacated), f8/g7/h7
    // are still blocked, so Kg8-h8 is forced right back. Ng7 (not a pawn)
    // is deliberately used instead of a second pawn so it can never capture
    // the knight on h6 (a pawn on g7 could, via g7xh6, breaking the loop),
    // and it also shields Bf8 from ever reaching h6 along the f8-h6
    // diagonal. No SearchLimits.history is supplied, so the only position
    // pre-loaded into the search's repetition history is the root itself
    // (history index 0); the position after Nf7+ (White knight f7, Black
    // king h8, Black to move) recurs 4 plies later purely through the
    // search's own move exploration (ply 1 and ply 5), which is a genuinely
    // different position from the root (root has the knight on d6, not f7,
    // and Black's king still on h8 in both, but reached with different
    // knight placement) - so the repeated position's earlier occurrence is
    // strictly deeper than the root, hitting is_repetition's `i > root`
    // branch, not the `occurrences >= 2` branch that the other repetition
    // tests exercise via pre-seeded history.
    Position p; p.set("5b1k/6np/3N4/8/8/8/8/6K1 w - - 0 1");
    SearchLimits lim; lim.depth = 6;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_D6, SQ_F7));
    CHECK(r.score == 0);
}

TEST_CASE("search_best_move writes into the caller-supplied transposition table") {
    attacks::init();
    zobrist::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    TranspositionTable tt(16);
    SearchLimits lim; lim.depth = 2;
    SearchResult r = search_best_move(p, lim, tt);
    StateInfo st;
    REQUIRE(r.best != MOVE_NONE);
    p.do_move(r.best, st);
    zobrist::Key child_key = p.key();
    p.undo_move(r.best, st);
    CHECK(tt.probe(child_key) != nullptr); // negamax stored an entry for the position after the root's best move
}

TEST_CASE("search_best_move reuses a warm caller-supplied table to search fewer nodes") {
    attacks::init();
    zobrist::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = 5;

    TranspositionTable warm(16);
    search_best_move(p, lim, warm);                       // first pass: fills the table
    SearchResult reused = search_best_move(p, lim, warm);  // second pass: same table, same position

    TranspositionTable cold(16);
    SearchResult fresh = search_best_move(p, lim, cold);   // never-seeded table, for comparison

    CHECK(reused.nodes < fresh.nodes);
}

TEST_CASE("nodes_limit stops the search close to the requested node budget") {
    attacks::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = MAX_DEPTH; lim.nodes_limit = 5000;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best != MOVE_NONE);
    // The 2048-node polling batch used by expired() means the search can run
    // a bit past the requested budget, but not by more than one full batch.
    CHECK(r.nodes < 5000 + 2048);
}

TEST_CASE("search_moves restricts the root to the given moves") {
    attacks::init();
    // 4k3/8/8/8/3q4/8/8/3RK3 w: Rxd4 wins the queen outright and is the
    // engine's normal choice (see "captures the free queen" above);
    // restrict the root to a different, clearly worse move and confirm the
    // search picks that one instead of Rxd4.
    Position p; p.set("4k3/8/8/8/3q4/8/8/3RK3 w - - 0 1");
    SearchLimits lim; lim.depth = 4;
    lim.search_moves = {make_move(SQ_E1, SQ_E2)};
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_E1, SQ_E2));
}

TEST_CASE("on_iteration fires once per completed depth with sane fields") {
    attacks::init();
    zobrist::init();
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = 3;
    std::vector<IterationInfo> seen;
    lim.on_iteration = [&seen](const IterationInfo& info) { seen.push_back(info); };
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);

    REQUIRE(seen.size() == 3); // one call per depth 1..3
    for (std::size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i].depth == static_cast<int>(i) + 1);
        CHECK(seen[i].seldepth >= seen[i].depth);
        CHECK(seen[i].multipv_index == 0);
        CHECK(seen[i].best != MOVE_NONE);
    }
    CHECK(seen.back().best == r.best);
    CHECK(seen.back().score == r.score);
}
