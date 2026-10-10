#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>

#include "uci.hpp"

namespace {

struct UciFixture {
    std::istringstream in;
    std::ostringstream out;
    chess::Uci uci{in, out};
};

}  // namespace

TEST_CASE_METHOD(UciFixture, "uci identifies the engine and ends with uciok", "[uci]") {
    REQUIRE(uci.handle_command("uci"));
    const std::string reply = out.str();
    CHECK(reply.starts_with("id name ChessEngine"));
    CHECK(reply.find("id author ") != std::string::npos);
    CHECK(reply.ends_with("uciok\n"));
}

TEST_CASE_METHOD(UciFixture, "isready answers readyok", "[uci]") {
    REQUIRE(uci.handle_command("isready"));
    CHECK(out.str() == "readyok\n");
}

TEST_CASE_METHOD(UciFixture, "go reports info per iteration and ends with bestmove", "[uci]") {
    REQUIRE(uci.handle_command("go depth 2"));
    uci.wait();
    const std::string reply = out.str();
    CHECK(reply.starts_with("info depth 1 seldepth "));
    CHECK(reply.find("\ninfo depth 2 ") != std::string::npos);
    CHECK(reply.find(" score cp ") != std::string::npos);
    CHECK(reply.find(" pv ") != std::string::npos);
    CHECK(reply.find("\nbestmove ") != std::string::npos);
    CHECK(reply.ends_with("\n"));
}

TEST_CASE_METHOD(UciFixture, "go reports mate scores in moves", "[uci]") {
    uci.handle_command("position fen 6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
    uci.handle_command("go depth 3");
    uci.wait();
    const std::string reply = out.str();
    CHECK(reply.find("score mate 1 ") != std::string::npos);
    CHECK(reply.ends_with("bestmove d1d8\n"));
}

TEST_CASE_METHOD(UciFixture, "go infinite waits for stop and isready is answered meanwhile",
                 "[uci]") {
    uci.handle_command("go infinite");
    REQUIRE(uci.handle_command("isready"));
    CHECK(out.str().find("readyok\n") != std::string::npos);
    CHECK(out.str().find("bestmove") == std::string::npos);
    REQUIRE(uci.handle_command("stop"));
    CHECK(out.str().find("\nbestmove ") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "go ponder waits for ponderhit, then plays on the clock", "[uci]") {
    using namespace std::chrono_literals;
    uci.handle_command("go ponder wtime 1000 btime 1000");
    // A search on this clock would have answered long before.
    std::this_thread::sleep_for(1200ms);
    REQUIRE(uci.handle_command("isready"));
    CHECK(out.str().find("bestmove") == std::string::npos);
    REQUIRE(uci.handle_command("ponderhit"));
    uci.wait();
    CHECK(out.str().find("\nbestmove ") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "stop ends pondering with a best move", "[uci]") {
    uci.handle_command("go ponder wtime 1000 btime 1000");
    REQUIRE(uci.handle_command("stop"));
    CHECK(out.str().find("\nbestmove ") != std::string::npos);
}

TEST_CASE("an infinite search is stopped at the end of input", "[uci]") {
    std::istringstream in("go infinite\n");
    std::ostringstream out;
    chess::Uci uci(in, out);
    uci.loop();
    CHECK(out.str().find("bestmove ") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "quit stops a running search", "[uci]") {
    uci.handle_command("go infinite");
    CHECK_FALSE(uci.handle_command("quit"));
    CHECK(out.str().find("bestmove ") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "the Hash option resizes the transposition table", "[uci]") {
    uci.handle_command("uci");
    CHECK(out.str().find("option name Hash type spin default 16 min 1 max 65536\n") !=
          std::string::npos);
    CHECK(out.str().find("option name Ponder type check default false\n") != std::string::npos);
    out.str("");
    uci.handle_command("setoption name Hash value 2");
    uci.handle_command("ucinewgame");
    CHECK(out.str().empty());
    uci.handle_command("setoption name Hash value 0");
    CHECK(out.str() == "info string invalid option Hash value 0\n");
}

TEST_CASE_METHOD(UciFixture, "go reports hashfull", "[uci]") {
    uci.handle_command("go depth 3");
    uci.wait();
    CHECK(out.str().find(" hashfull ") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "go with a clock answers in time", "[uci]") {
    uci.handle_command("go wtime 200 btime 200 winc 0 binc 0");
    uci.wait();
    CHECK(out.str().find("bestmove ") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "uci lists the options and setoption validates values", "[uci]") {
    uci.handle_command("uci");
    CHECK(out.str().find("option name Move Overhead type spin default 30 min 0 max 5000\n") !=
          std::string::npos);
    out.str("");
    uci.handle_command("setoption name Move Overhead value 50");
    CHECK(out.str().empty());
    uci.handle_command("setoption name Move Overhead value -1");
    CHECK(out.str() == "info string invalid option Move Overhead value -1\n");
}

TEST_CASE_METHOD(UciFixture, "quit stops the engine", "[uci]") {
    CHECK_FALSE(uci.handle_command("quit"));
    CHECK_FALSE(uci.handle_command("  quit  "));
}

TEST_CASE_METHOD(UciFixture, "unknown and empty commands are ignored", "[uci]") {
    CHECK(uci.handle_command(""));
    CHECK(uci.handle_command("   "));
    CHECK(uci.handle_command("xyzzy"));
    CHECK(out.str().empty());
}

TEST_CASE_METHOD(UciFixture, "position startpos with moves", "[uci][position]") {
    uci.handle_command("position startpos moves e2e4 e7e5 g1f3");
    CHECK(uci.position().fen() == "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2");
}

TEST_CASE_METHOD(UciFixture, "position fen applies moves", "[uci][position]") {
    const std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    uci.handle_command("position fen " + fen + " moves c7c5");
    CHECK(uci.position().fen() == "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
}

TEST_CASE_METHOD(UciFixture, "ucinewgame resets the position", "[uci][position]") {
    uci.handle_command("position startpos moves e2e4");
    uci.handle_command("ucinewgame");
    CHECK(uci.position().fen() == chess::Position::kStartFen);
}

TEST_CASE_METHOD(UciFixture, "malformed position commands are ignored", "[uci][position]") {
    uci.handle_command("position startpos moves e2e4");
    const std::string expected = uci.position().fen();
    uci.handle_command("position");
    uci.handle_command("position fen");
    uci.handle_command("position fen not/a/fen w - - 0 1");
    uci.handle_command("position bogus");
    uci.handle_command("position startpos moves e2e4 e7e4");
    CHECK(uci.position().fen() == expected);
}

TEST_CASE_METHOD(UciFixture, "go perft prints the divide and the node count", "[uci][perft]") {
    uci.handle_command("position startpos");
    REQUIRE(uci.handle_command("go perft 2"));
    const std::string reply = out.str();
    CHECK(reply.find("e2e4: 20\n") != std::string::npos);
    CHECK(reply.find("g1f3: 20\n") != std::string::npos);
    CHECK(reply.find("\nNodes searched: 400\n") != std::string::npos);
}

TEST_CASE_METHOD(UciFixture, "go plays a legal move, or the null move when there is none",
                 "[uci]") {
    uci.handle_command("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");  // Stalemate.
    uci.handle_command("go depth 1");
    uci.wait();
    CHECK(out.str().ends_with("bestmove 0000\n"));
}

TEST_CASE_METHOD(UciFixture, "d shows the board and the fen", "[uci]") {
    uci.handle_command("d");
    CHECK(out.str().find(std::string("Fen: ") + std::string(chess::Position::kStartFen)) !=
          std::string::npos);
}

TEST_CASE("loop processes commands until quit", "[uci]") {
    std::istringstream in("uci\nisready\nquit\nisready\n");
    std::ostringstream out;
    chess::Uci uci(in, out);
    uci.loop();
    const std::string reply = out.str();
    CHECK(reply.ends_with("uciok\nreadyok\n"));
}
