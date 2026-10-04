#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

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

TEST_CASE_METHOD(UciFixture, "go answers with a bestmove", "[uci]") {
    REQUIRE(uci.handle_command("go depth 1"));
    CHECK(out.str().starts_with("bestmove "));
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
    CHECK(uci.position().fen.empty());
    CHECK(uci.position().moves == std::vector<std::string>{"e2e4", "e7e5", "g1f3"});
}

TEST_CASE_METHOD(UciFixture, "position fen keeps all six fields", "[uci][position]") {
    const std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    uci.handle_command("position fen " + fen + " moves c7c5");
    CHECK(uci.position().fen == fen);
    CHECK(uci.position().moves == std::vector<std::string>{"c7c5"});
}

TEST_CASE_METHOD(UciFixture, "ucinewgame resets the position", "[uci][position]") {
    uci.handle_command("position startpos moves e2e4");
    uci.handle_command("ucinewgame");
    CHECK(uci.position().fen.empty());
    CHECK(uci.position().moves.empty());
}

TEST_CASE_METHOD(UciFixture, "malformed position commands are ignored", "[uci][position]") {
    uci.handle_command("position startpos moves e2e4");
    uci.handle_command("position");
    uci.handle_command("position fen");
    uci.handle_command("position bogus");
    CHECK(uci.position().moves == std::vector<std::string>{"e2e4"});
}

TEST_CASE("loop processes commands until quit", "[uci]") {
    std::istringstream in("uci\nisready\nquit\nisready\n");
    std::ostringstream out;
    chess::Uci uci(in, out);
    uci.loop();
    const std::string reply = out.str();
    CHECK(reply.ends_with("uciok\nreadyok\n"));
}
