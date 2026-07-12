#include "doctest.h"
#include "uci.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "types.hpp"
#include "search.hpp"
using namespace chess;

TEST_CASE("move <-> uci string") {
    attacks::init();
    CHECK(move_to_uci(make_move(SQ_E2, SQ_E4)) == "e2e4");
    CHECK(move_to_uci(make_move(SQ_A7, SQ_A8, PROMOTION, QUEEN)) == "a7a8q");
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(uci_to_move(p, "e2e4") == make_move(SQ_E2, SQ_E4));
}

TEST_CASE("format_score formats centipawn and mate scores per the UCI spec") {
    CHECK(format_score(37) == "cp 37");
    CHECK(format_score(-120) == "cp -120");
    CHECK(format_score(0) == "cp 0");
    // Mate found for the side to move: MATE - ply, e.g. mate in 1 ply (mate
    // in 1 move) scores MATE - 1.
    CHECK(format_score(MATE - 1) == "mate 1");
    // Mate in 4 plies == mate in 2 moves.
    CHECK(format_score(MATE - 4) == "mate 2");
    // Getting mated (negative): symmetric, negative move count.
    CHECK(format_score(-(MATE - 1)) == "mate -1");
    CHECK(format_score(-(MATE - 4)) == "mate -2");
}

TEST_CASE("compute_move_time budgets the clock") {
    // Sudden death, 60s each, White to move: 60000/30 - 30 overhead = 1970.
    CHECK(compute_move_time(WHITE, 60000, 60000, 0, 0, 0) == 1970);
    // Uses the side-to-move's own clock: Black low on time.
    CHECK(compute_move_time(BLACK, 60000, 1000, 0, 0, 0) == 3);
    // movestogo=1 but capped at 80% of the clock (1000*4/5 - 30 = 770).
    CHECK(compute_move_time(WHITE, 1000, 1000, 0, 0, 1) == 770);
    // Increment adds 3/4 of the increment: 10000/30 + 750 - 30 = 1053.
    CHECK(compute_move_time(WHITE, 10000, 10000, 1000, 1000, 0) == 1053);
    // No time on our clock -> minimal positive budget.
    CHECK(compute_move_time(WHITE, 0, 60000, 0, 0, 0) == 1);
}

TEST_CASE("build_game_history returns one key per position including the start") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    auto history = build_game_history(p, {"e2e4", "e7e5"});
    CHECK(history.size() == 3);
    CHECK(history[0] != history[1]);
    CHECK(history[1] != history[2]);

    Position p2; p2.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    StateInfo st;
    p2.do_move(uci_to_move(p2, "e2e4"), st);
    p2.do_move(uci_to_move(p2, "e7e5"), st);
    CHECK(history[2] == p2.key());
    CHECK(p.fen() == p2.fen()); // build_game_history left p at the final position
}

TEST_CASE("build_game_history stops at the first illegal move") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    auto history = build_game_history(p, {"e2e4", "bogus", "e7e5"});
    CHECK(history.size() == 2); // start position + e2e4 only
}

TEST_CASE("parse_setoption splits name and value") {
    SetOption opt = parse_setoption({"setoption", "name", "Hash", "value", "64"});
    CHECK(opt.name == "Hash");
    CHECK(opt.value == "64");
}

TEST_CASE("parse_setoption handles a multi-word option name with no value") {
    SetOption opt = parse_setoption({"setoption", "name", "Clear", "Hash"});
    CHECK(opt.name == "Clear Hash");
    CHECK(opt.value == "");
}

TEST_CASE("parse_setoption handles a multi-word value") {
    SetOption opt = parse_setoption(
        {"setoption", "name", "Debug", "Log", "File", "value", "log", "file.txt"});
    CHECK(opt.name == "Debug Log File");
    CHECK(opt.value == "log file.txt");
}

TEST_CASE("option_lines advertises Hash, Ponder, and MultiPV") {
    auto lines = option_lines();
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "option name Hash type spin default 16 min 1 max 1024");
    CHECK(lines[1] == "option name Ponder type check default false");
    CHECK(lines[2] == "option name MultiPV type spin default 1 min 1 max 256");
}

TEST_CASE("parse_debug_command turns debug on") {
    CHECK(parse_debug_command({"debug", "on"}, false) == true);
}

TEST_CASE("parse_debug_command turns debug off") {
    CHECK(parse_debug_command({"debug", "off"}, true) == false);
}

TEST_CASE("parse_debug_command ignores a malformed command") {
    CHECK(parse_debug_command({"debug"}, true) == true);
    CHECK(parse_debug_command({"debug", "maybe"}, false) == false);
}

TEST_CASE("extract_pv walks the TT's stored best moves into a move sequence") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    TranspositionTable tt(16);

    Move e2e4 = uci_to_move(p, "e2e4");
    StateInfo st1; p.do_move(e2e4, st1);
    Move e7e5 = uci_to_move(p, "e7e5");
    tt.store(p.key(), 1, 0, TT_EXACT, e7e5); // TT entry for the position after e2e4
    p.undo_move(e2e4, st1);

    std::vector<Move> pv = extract_pv(p, tt, e2e4, 5);
    REQUIRE(pv.size() == 2);
    CHECK(pv[0] == e2e4);
    CHECK(pv[1] == e7e5);
    CHECK(p.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); // pos restored
}

TEST_CASE("extract_pv stops when the TT has no entry for the reached position") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    TranspositionTable tt(16); // empty
    Move e2e4 = uci_to_move(p, "e2e4");
    std::vector<Move> pv = extract_pv(p, tt, e2e4, 5);
    REQUIRE(pv.size() == 1);
    CHECK(pv[0] == e2e4);
    CHECK(p.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); // pos restored
}

TEST_CASE("extract_pv stops at a TT-stored move that isn't legal in the reached position") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    TranspositionTable tt(16);
    Move e2e4 = uci_to_move(p, "e2e4");
    StateInfo st1; p.do_move(e2e4, st1);
    // Store a move that is not legal after 1.e4 (still White's pawn on e2 in
    // this stored move, which no longer exists there).
    tt.store(p.key(), 1, 0, TT_EXACT, make_move(SQ_E2, SQ_E4));
    p.undo_move(e2e4, st1);

    std::vector<Move> pv = extract_pv(p, tt, e2e4, 5);
    REQUIRE(pv.size() == 1);
    CHECK(pv[0] == e2e4);
    CHECK(p.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); // pos restored
}

TEST_CASE("extract_pv respects max_len") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    TranspositionTable tt(16);
    Move e2e4 = uci_to_move(p, "e2e4");
    StateInfo st1; p.do_move(e2e4, st1);
    Move e7e5 = uci_to_move(p, "e7e5");
    tt.store(p.key(), 1, 0, TT_EXACT, e7e5);
    p.undo_move(e2e4, st1);

    std::vector<Move> pv = extract_pv(p, tt, e2e4, 1);
    REQUIRE(pv.size() == 1);
    CHECK(pv[0] == e2e4);
    CHECK(p.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); // pos restored
}

TEST_CASE("parse_go reads depth and marks depth_set") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    GoOptions g = parse_go(p, {"go", "depth", "10"});
    CHECK(g.depth == 10);
    CHECK(g.depth_set);
}

TEST_CASE("parse_go reads movetime and marks movetime_set") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    GoOptions g = parse_go(p, {"go", "movetime", "500"});
    CHECK(g.movetime_ms == 500);
    CHECK(g.movetime_set);
}

TEST_CASE("parse_go reads clock and increment fields") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    GoOptions g = parse_go(p, {"go", "wtime", "60000", "btime", "50000",
                                "winc", "1000", "binc", "500", "movestogo", "20"});
    CHECK(g.wtime == 60000);
    CHECK(g.btime == 50000);
    CHECK(g.winc == 1000);
    CHECK(g.binc == 500);
    CHECK(g.movestogo == 20);
}

TEST_CASE("parse_go reads infinite and ponder flags") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(parse_go(p, {"go", "infinite"}).infinite);
    CHECK(parse_go(p, {"go", "ponder"}).ponder);
    CHECK_FALSE(parse_go(p, {"go", "depth", "1"}).infinite);
}

TEST_CASE("parse_go reads a nodes limit") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    GoOptions g = parse_go(p, {"go", "nodes", "50000"});
    CHECK(g.nodes_limit == 50000);
}

TEST_CASE("parse_go resolves searchmoves to legal Move values and keeps parsing after them") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    GoOptions g = parse_go(p, {"go", "searchmoves", "e2e4", "d2d4", "wtime", "1000"});
    REQUIRE(g.search_moves.size() == 2);
    CHECK(g.search_moves[0] == make_move(SQ_E2, SQ_E4));
    CHECK(g.search_moves[1] == make_move(SQ_D2, SQ_D4));
    CHECK(g.wtime == 1000);
}

TEST_CASE("parse_go stops searchmoves at the first token that isn't a legal move") {
    attacks::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    GoOptions g = parse_go(p, {"go", "searchmoves", "e2e4", "bogus"});
    CHECK(g.search_moves.size() == 1);
}
