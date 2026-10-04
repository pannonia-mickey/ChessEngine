#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "evaluate.hpp"
#include "test_helpers.hpp"

using namespace chess;

namespace {

// The same position with colors swapped and the board flipped vertically.
std::string mirror_fen(std::string_view fen) {
    std::vector<std::string> fields;
    for (const auto field : std::views::split(fen, ' ')) {
        fields.emplace_back(field.begin(), field.end());
    }
    const auto swap_case = [](std::string text) {
        for (char& c : text) {
            const auto u = static_cast<unsigned char>(c);
            c = static_cast<char>(std::isupper(u) != 0 ? std::tolower(u) : std::toupper(u));
        }
        return text;
    };
    std::vector<std::string> ranks;
    for (const auto rank : std::views::split(fields.at(0), '/')) {
        ranks.emplace_back(rank.begin(), rank.end());
    }
    std::ranges::reverse(ranks);
    std::string board;
    for (const auto& rank : ranks) {
        board += (board.empty() ? "" : "/") + swap_case(rank);
    }
    std::string castling = fields.at(2) == "-" ? "-" : swap_case(fields.at(2));
    std::ranges::sort(castling, [](char a, char b) {
        // FEN order: KQkq.
        const auto rank = [](char c) { return std::string_view("KQkq-").find(c); };
        return rank(a) < rank(b);
    });
    std::string en_passant = fields.at(3);
    if (en_passant != "-") {
        en_passant[1] = en_passant[1] == '3' ? '6' : '3';
    }
    return board + ' ' + (fields.at(1) == "w" ? "b" : "w") + ' ' + castling + ' ' + en_passant +
           " 0 1";
}

}  // namespace

TEST_CASE("the starting position evaluates to zero", "[evaluate]") {
    const Position pos;
    CHECK(evaluate(pos) == 0);
    CHECK(game_phase(pos) == kMaxPhase);
}

TEST_CASE("evaluation is symmetric under color mirroring", "[evaluate]") {
    for (const auto* const fen : {
             "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
             "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
             "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
             "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
         }) {
        INFO(fen);
        const Position pos = test::position_from(fen);
        const Position mirrored = test::position_from(mirror_fen(fen));
        CHECK(evaluate(pos) == evaluate(mirrored));
    }
}

TEST_CASE("evaluation is from the side to move's point of view", "[evaluate]") {
    // White is a queen up.
    const Position white_to_move = test::position_from("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    const Position black_to_move = test::position_from("4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
    CHECK(evaluate(white_to_move) > 800);
    CHECK(evaluate(black_to_move) == -evaluate(white_to_move));
}

TEST_CASE("game phase counts minor and major pieces", "[evaluate]") {
    CHECK(game_phase(test::position_from("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1")) == 0);
    CHECK(game_phase(test::position_from("3qk3/8/8/8/8/8/8/2R1K1N1 w - - 0 1")) == 7);
}
