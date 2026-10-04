#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "perft.hpp"
#include "position.hpp"
#include "test_helpers.hpp"

using namespace chess;

namespace {

struct PerftCase {
    std::string name;
    std::string fen;
    // Expected node counts for depth 1, 2, ...
    std::vector<std::uint64_t> nodes;
};

void check_perft(const PerftCase& test) {
    INFO(test.name << ": " << test.fen);
    Position pos = test::position_from(test.fen);
    for (std::size_t i = 0; i < test.nodes.size(); ++i) {
        const int depth = static_cast<int>(i) + 1;
        INFO("depth " << depth);
        CHECK(perft(pos, depth) == test.nodes[i]);
    }
    CHECK(pos.fen() == test::position_from(test.fen).fen());
}

struct EdgeCase {
    std::string name;
    std::string fen;
    int depth = 0;
    std::uint64_t nodes = 0;
};

}  // namespace

// Reference counts from https://www.chessprogramming.org/Perft_Results, cut off at depths that
// keep debug and sanitizer builds fast.
TEST_CASE("perft on the standard positions", "[perft]") {
    const PerftCase test = GENERATE(values<PerftCase>({
        {"startpos", std::string(Position::kStartFen), {20, 400, 8'902, 197'281, 4'865'609}},
        {"kiwipete",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2'039, 97'862, 4'085'603}},
        {"position 3",
         "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         {14, 191, 2'812, 43'238, 674'624}},
        {"position 4",
         "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9'467, 422'333}},
        {"position 4 mirrored",
         "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
         {6, 264, 9'467, 422'333}},
        {"position 5",
         "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         {44, 1'486, 62'379, 2'103'487}},
        {"position 6",
         "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2'079, 89'890, 3'894'594}},
    }));
    check_perft(test);
}

// Positions targeting individual rules, from the well-known suite by Martin Sedlak, at the
// depth the suite publishes.
TEST_CASE("perft on rule edge cases", "[perft]") {
    const auto [name, fen, depth, nodes] = GENERATE(values<EdgeCase>({
        {"illegal en passant 1", "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1'134'888},
        {"illegal en passant 2", "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1'015'133},
        {"en passant gives check", "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1'440'467},
        {"short castling gives check", "5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661'072},
        {"long castling gives check", "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803'711},
        {"castling rights", "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1'274'206},
        {"castling prevented", "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1'720'476},
        {"promote out of check", "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3'821'001},
        {"discovered check", "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1'004'658},
        {"promote to give check", "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217'342},
        {"underpromote to give check", "8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92'683},
        {"self stalemate", "K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2'217},
        {"stalemate and checkmate", "8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567'584},
        {"double check", "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23'527},
    }));
    INFO(name << ": " << fen);
    Position pos = test::position_from(fen);
    CHECK(perft(pos, depth) == nodes);
}

TEST_CASE("perft divide sums to perft", "[perft]") {
    Position pos;
    std::uint64_t total = 0;
    const auto entries = perft_divide(pos, 3);
    CHECK(entries.size() == 20);
    for (const auto& entry : entries) {
        total += entry.nodes;
    }
    CHECK(total == 8'902);
}

TEST_CASE("perft depth 0 counts the root", "[perft]") {
    Position pos;
    CHECK(perft(pos, 0) == 1);
}

// Deeper runs for release builds: ctest skips hidden tests, run with `chess_tests "[.deep]"`.
TEST_CASE("perft deep", "[.deep][perft]") {
    const PerftCase test = GENERATE(values<PerftCase>({
        {"startpos",
         std::string(Position::kStartFen),
         {20, 400, 8'902, 197'281, 4'865'609, 119'060'324}},
        {"kiwipete",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2'039, 97'862, 4'085'603, 193'690'690}},
        {"position 3",
         "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         {14, 191, 2'812, 43'238, 674'624, 11'030'083, 178'633'661}},
        {"position 4",
         "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9'467, 422'333, 15'833'292}},
        {"position 5",
         "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         {44, 1'486, 62'379, 2'103'487, 89'941'194}},
        {"position 6",
         "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2'079, 89'890, 3'894'594, 164'075'551}},
    }));
    check_perft(test);
}
