#pragma once

#include "types.hpp"
#include <bit>
#include <cstdint>
#include <string>

namespace chess {

using Bitboard = std::uint64_t;

// Bitboard constants for files
constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_B_BB = FILE_A_BB << 1;
constexpr Bitboard FILE_C_BB = FILE_A_BB << 2;
constexpr Bitboard FILE_D_BB = FILE_A_BB << 3;
constexpr Bitboard FILE_E_BB = FILE_A_BB << 4;
constexpr Bitboard FILE_F_BB = FILE_A_BB << 5;
constexpr Bitboard FILE_G_BB = FILE_A_BB << 6;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;

// Bitboard constants for ranks
constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << 8;
constexpr Bitboard RANK_3_BB = RANK_1_BB << 16;
constexpr Bitboard RANK_4_BB = RANK_1_BB << 24;
constexpr Bitboard RANK_5_BB = RANK_1_BB << 32;
constexpr Bitboard RANK_6_BB = RANK_1_BB << 40;
constexpr Bitboard RANK_7_BB = RANK_1_BB << 48;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;

// Create a bitboard with a single bit set at square s
constexpr Bitboard square_bb(Square s) {
    return 1ULL << s;
}

// Count the number of set bits
inline int popcount(Bitboard b) {
    return std::popcount(b);
}

// Get the least significant bit (as a Square)
inline Square lsb(Bitboard b) {
    return Square(std::countr_zero(b));
}

// Pop the least significant bit (remove it and return it)
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

// Directional shifts with edge masking to prevent wraparound
constexpr Bitboard shift_north(Bitboard b) {
    return b << 8;
}

constexpr Bitboard shift_south(Bitboard b) {
    return b >> 8;
}

constexpr Bitboard shift_east(Bitboard b) {
    return (b & ~FILE_H_BB) << 1;
}

constexpr Bitboard shift_west(Bitboard b) {
    return (b & ~FILE_A_BB) >> 1;
}

// Diagonal shifts with edge masking to prevent wraparound
constexpr Bitboard shift_north_east(Bitboard b) {
    return (b & ~FILE_H_BB) << 9;
}

constexpr Bitboard shift_north_west(Bitboard b) {
    return (b & ~FILE_A_BB) << 7;
}

constexpr Bitboard shift_south_east(Bitboard b) {
    return (b & ~FILE_H_BB) >> 7;
}

constexpr Bitboard shift_south_west(Bitboard b) {
    return (b & ~FILE_A_BB) >> 9;
}

// Debug function to print bitboard as an 8x8 grid
std::string pretty(Bitboard b);

} // namespace chess
