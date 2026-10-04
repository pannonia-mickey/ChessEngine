#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "bitboard.hpp"
#include "magic.hpp"

using namespace chess;

namespace {

// xorshift64 for reproducible test occupancies.
std::uint64_t next_random(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

}  // namespace

TEST_CASE("leaper attack tables", "[bitboard]") {
    CHECK(popcount(knight_attacks(A1)) == 2);
    CHECK(popcount(knight_attacks(D4)) == 8);
    CHECK(knight_attacks(G1) == (square_bb(E2) | square_bb(F3) | square_bb(H3)));
    CHECK(popcount(king_attacks(H8)) == 3);
    CHECK(popcount(king_attacks(E4)) == 8);
    CHECK(pawn_attacks(White, E4) == (square_bb(D5) | square_bb(F5)));
    CHECK(pawn_attacks(Black, E4) == (square_bb(D3) | square_bb(F3)));
    CHECK(pawn_attacks(White, A2) == square_bb(B3));
    CHECK(pawn_attacks(Black, H7) == square_bb(G6));
}

TEST_CASE("shifts drop squares that leave the board", "[bitboard]") {
    CHECK(shift_east(square_bb(H4)) == 0);
    CHECK(shift_west(square_bb(A4)) == 0);
    CHECK(shift_north(square_bb(E8)) == 0);
    CHECK(shift_south(square_bb(E1)) == 0);
    CHECK(shift_east(square_bb(D4)) == square_bb(E4));
}

TEST_CASE("magic slider attacks match the ray-walking reference", "[bitboard][magic]") {
    std::uint64_t state = 0x1234'5678'9ABC'DEF1ULL;
    for (int sq = 0; sq < kSquareCount; ++sq) {
        const auto square = static_cast<Square>(sq);
        for (int i = 0; i < 300; ++i) {
            // Sparse and dense occupancies both matter.
            const Bitboard first = next_random(state);
            const Bitboard second = next_random(state);
            const Bitboard occupancy = (i % 2 == 0) ? (first & second) : (first | second);
            REQUIRE(rook_attacks(square, occupancy) == sliding_attacks(Rook, square, occupancy));
            REQUIRE(bishop_attacks(square, occupancy) ==
                    sliding_attacks(Bishop, square, occupancy));
        }
    }
}

TEST_CASE("magic masks exclude board edges", "[bitboard][magic]") {
    CHECK(popcount(slider_tables().rook_magic(A1).mask) == 12);
    CHECK(popcount(slider_tables().rook_magic(E4).mask) == 10);
    CHECK(popcount(slider_tables().bishop_magic(A1).mask) == 6);
    CHECK(popcount(slider_tables().bishop_magic(E4).mask) == 9);
}

TEST_CASE("between and line tables", "[bitboard]") {
    CHECK(between_bb(A1, H8) == (square_bb(B2) | square_bb(C3) | square_bb(D4) | square_bb(E5) |
                                 square_bb(F6) | square_bb(G7)));
    CHECK(between_bb(E1, E3) == square_bb(E2));
    CHECK(between_bb(E1, E2) == 0);
    CHECK(between_bb(A1, B3) == 0);
    CHECK(line_bb(C3, E5) == (square_bb(A1) | square_bb(B2) | square_bb(C3) | square_bb(D4) |
                              square_bb(E5) | square_bb(F6) | square_bb(G7) | square_bb(H8)));
    CHECK(line_bb(B1, B5) == file_bb(1));
    CHECK(line_bb(A1, B3) == 0);
}
