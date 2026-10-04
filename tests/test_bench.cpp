#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "bench.hpp"
#include "position.hpp"
#include "uci.hpp"

using namespace chess;

TEST_CASE("every bench position is a valid fen", "[bench]") {
    for (const auto fen : kBenchFens) {
        INFO(fen);
        CHECK(Position::from_fen(fen).has_value());
    }
}

TEST_CASE("bench is deterministic and ends with the nodes/nps line", "[bench]") {
    std::ostringstream first;
    std::ostringstream second;
    const BenchResult a = run_bench(first, 2);
    const BenchResult b = run_bench(second, 2);
    CHECK(a.nodes == b.nodes);
    CHECK(a.nodes > 0);

    const std::string text = first.str();
    const std::string expected_tail = " nodes " + std::to_string(a.nps()) + " nps\n";
    CHECK(text.ends_with(std::to_string(a.nodes) + expected_tail));
    CHECK(text.find("Nodes searched  : " + std::to_string(a.nodes) + '\n') != std::string::npos);
}

TEST_CASE("bench through uci accepts a depth and rejects garbage", "[bench][uci]") {
    std::istringstream in;
    std::ostringstream out;
    Uci uci(in, out);

    REQUIRE(uci.handle_command("bench 1"));
    std::ostringstream direct;
    const BenchResult expected = run_bench(direct, 1);
    CHECK(out.str().find("Nodes searched  : " + std::to_string(expected.nodes) + '\n') !=
          std::string::npos);

    out.str("");
    REQUIRE(uci.handle_command("bench zero"));
    CHECK(out.str() == "info string invalid bench depth zero\n");
}
