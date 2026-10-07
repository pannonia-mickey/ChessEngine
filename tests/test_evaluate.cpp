#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "eval_values.hpp"
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

TEST_CASE("the trace reproduces the evaluation", "[evaluate]") {
    for (const auto* const fen : {
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
             "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
             "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
             "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
         }) {
        INFO(fen);
        const Position pos = test::position_from(fen);
        const EvalTrace trace = trace_evaluation(pos);
        int mg = 0;
        int eg = 0;
        for (std::size_t i = 0; i < eval::kParamValues.size(); ++i) {
            mg += trace.coefficients.at(i) * eval::kParamValues.at(i).mg;
            eg += trace.coefficients.at(i) * eval::kParamValues.at(i).eg;
        }
        const int white_score = ((mg * trace.phase) + (eg * (kMaxPhase - trace.phase))) / kMaxPhase;
        CHECK(trace.phase == game_phase(pos));
        CHECK(white_score == (pos.side_to_move() == White ? evaluate(pos) : -evaluate(pos)));
    }
}

namespace {

int coefficient(std::string_view fen, int index) {
    return trace_evaluation(test::position_from(fen))
        .coefficients.at(static_cast<std::size_t>(index));
}

}  // namespace

TEST_CASE("pawn structure terms", "[evaluate]") {
    // Doubled and isolated e-pawns: only the front one is passed.
    const auto* const doubled = "4k3/8/8/8/8/4P3/4P3/4K3 w - - 0 1";
    CHECK(coefficient(doubled, eval::kDoubledPawn) == 1);
    CHECK(coefficient(doubled, eval::kIsolatedPawn) == 2);
    CHECK(coefficient(doubled, eval::kPassedPawn + 2) == 1);
    CHECK(coefficient(doubled, eval::kPassedPawn + 1) == 0);
    // The same for Black counts negatively.
    CHECK(coefficient("4k3/4p3/4p3/8/8/8/8/4K3 w - - 0 1", eval::kDoubledPawn) == -1);

    // e3 cannot be defended by a pawn and its stop square is attacked by d5; d5 is isolated.
    const auto* const backward = "4k3/8/8/3p4/3P4/4P3/8/4K3 w - - 0 1";
    CHECK(coefficient(backward, eval::kBackwardPawn) == 1);
    CHECK(coefficient(backward, eval::kIsolatedPawn) == -1);
    CHECK(coefficient(backward, eval::kSupportedPawn + 3) == 1);
    CHECK(coefficient(backward, eval::kPassedPawn + 3) == 0);

    const auto* const phalanx = "4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1";
    CHECK(coefficient(phalanx, eval::kPhalanxPawn + 3) == 2);
    CHECK(coefficient(phalanx, eval::kPassedPawn + 3) == 2);
}

TEST_CASE("piece and king terms", "[evaluate]") {
    CHECK(coefficient("4k3/8/8/8/8/8/8/2B1KB2 w - - 0 1", eval::kBishopPair) == 1);
    CHECK(coefficient("4k3/8/8/8/8/8/8/2B1K1N1 w - - 0 1", eval::kBishopPair) == 0);

    // The a-file is open, the h-file half open for White.
    const auto* const rooks = "4k3/7p/8/8/8/8/8/R3K2R w - - 0 1";
    CHECK(coefficient(rooks, eval::kRookOpenFile) == 1);
    CHECK(coefficient(rooks, eval::kRookSemiOpenFile) == 1);

    // A knight in the corner reaches two squares, one of them guarded by a pawn.
    CHECK(coefficient("4k3/8/8/8/p7/8/8/N3K3 w - - 0 1", eval::kKnightMobility + 1) == 1);

    // The castled king has three shield pawns; the queen attacks two squares around it.
    const auto* const castled = "6k1/8/8/8/8/8/5PPP/3q2K1 w - - 0 1";
    CHECK(coefficient(castled, eval::kPawnShield) == 3);
    CHECK(coefficient(castled, eval::kKingZoneAttack + 3) == -2);
    CHECK(coefficient(castled, eval::kKingAttackers + 1) == -1);
}
