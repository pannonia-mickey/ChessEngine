#pragma once

#include <array>
#include <bit>
#include <string>

#include "types.hpp"

namespace chess {

constexpr Bitboard kFileA = 0x0101010101010101ULL;
constexpr Bitboard kFileH = kFileA << 7;
constexpr Bitboard kRank1 = 0xFFULL;
constexpr Bitboard kRank8 = kRank1 << 56;

[[nodiscard]] constexpr Bitboard square_bb(Square square) noexcept {
    return Bitboard{1} << square;
}

[[nodiscard]] constexpr Bitboard file_bb(int file) noexcept {
    return kFileA << file;
}

[[nodiscard]] constexpr Bitboard rank_bb(int rank) noexcept {
    return kRank1 << (8 * rank);
}

[[nodiscard]] constexpr bool has(Bitboard bb, Square square) noexcept {
    return (bb & square_bb(square)) != 0;
}

[[nodiscard]] constexpr int popcount(Bitboard bb) noexcept {
    return std::popcount(bb);
}

[[nodiscard]] constexpr bool more_than_one(Bitboard bb) noexcept {
    return (bb & (bb - 1)) != 0;
}

// Least significant set square. Undefined for an empty bitboard.
[[nodiscard]] constexpr Square lsb(Bitboard bb) noexcept {
    return static_cast<Square>(std::countr_zero(bb));
}

// Removes and returns the least significant set square. Undefined for an empty bitboard.
constexpr Square pop_lsb(Bitboard& bb) noexcept {
    const Square square = lsb(bb);
    bb &= bb - 1;
    return square;
}

// Shifts every set square one step in a compass direction, dropping squares that leave the board.
[[nodiscard]] constexpr Bitboard shift_north(Bitboard bb) noexcept {
    return bb << 8;
}
[[nodiscard]] constexpr Bitboard shift_south(Bitboard bb) noexcept {
    return bb >> 8;
}
[[nodiscard]] constexpr Bitboard shift_east(Bitboard bb) noexcept {
    return (bb & ~kFileH) << 1;
}
[[nodiscard]] constexpr Bitboard shift_west(Bitboard bb) noexcept {
    return (bb & ~kFileA) >> 1;
}

namespace detail {

// Attacks of a piece that moves by fixed (file, rank) steps, computed at compile time.
template <std::size_t N>
consteval std::array<Bitboard, kSquareCount> leaper_attacks(
    const std::array<std::array<int, 2>, N>& steps) {
    std::array<Bitboard, kSquareCount> table{};
    for (int square = 0; square < kSquareCount; ++square) {
        const int file = square % 8;
        const int rank = square / 8;
        for (const auto& [file_step, rank_step] : steps) {
            const int to_file = file + file_step;
            const int to_rank = rank + rank_step;
            if (to_file >= 0 && to_file < 8 && to_rank >= 0 && to_rank < 8) {
                table.at(static_cast<std::size_t>(square)) |=
                    square_bb(make_square(to_file, to_rank));
            }
        }
    }
    return table;
}

constexpr std::array<std::array<int, 2>, 8> kKnightSteps{
    {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
constexpr std::array<std::array<int, 2>, 8> kKingSteps{
    {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}}};
constexpr std::array<std::array<int, 2>, 2> kWhitePawnSteps{{{-1, 1}, {1, 1}}};
constexpr std::array<std::array<int, 2>, 2> kBlackPawnSteps{{{-1, -1}, {1, -1}}};

constexpr auto kKnightAttacks = leaper_attacks(kKnightSteps);
constexpr auto kKingAttacks = leaper_attacks(kKingSteps);
constexpr std::array<std::array<Bitboard, kSquareCount>, kColorCount> kPawnAttacks{
    leaper_attacks(kWhitePawnSteps), leaper_attacks(kBlackPawnSteps)};

}  // namespace detail

[[nodiscard]] constexpr Bitboard pawn_attacks(Color color, Square square) noexcept {
    return detail::kPawnAttacks[color][square];
}

[[nodiscard]] constexpr Bitboard knight_attacks(Square square) noexcept {
    return detail::kKnightAttacks[square];
}

[[nodiscard]] constexpr Bitboard king_attacks(Square square) noexcept {
    return detail::kKingAttacks[square];
}

// Renders a bitboard as an 8x8 grid (rank 8 first), for debugging and tests.
[[nodiscard]] std::string to_string(Bitboard bb);

}  // namespace chess
