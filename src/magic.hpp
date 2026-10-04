#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "bitboard.hpp"
#include "types.hpp"

namespace chess {

// A "fancy" magic: maps every relevant blocker subset of one square to a unique slot of a
// shared attack table via (occupancy & mask) * magic >> shift.
struct Magic {
    Bitboard mask = 0;
    Bitboard magic = 0;
    std::uint32_t offset = 0;
    std::uint8_t shift = 0;

    [[nodiscard]] constexpr std::size_t index(Bitboard occupancy) const noexcept {
        return offset + (((occupancy & mask) * magic) >> shift);
    }
};

// Sum of 2^popcount(mask) over all squares.
constexpr std::size_t kRookTableSize = 102'400;
constexpr std::size_t kBishopTableSize = 5'248;

// Slider attack tables. The magic numbers are not hardcoded: the constructor searches for them
// with a seeded pseudo-random generator, so the result is deterministic across runs.
class SliderTables {
public:
    SliderTables();

    [[nodiscard]] Bitboard rook(Square square, Bitboard occupancy) const noexcept {
        return rook_table_[rook_magics_[square].index(occupancy)];
    }

    [[nodiscard]] Bitboard bishop(Square square, Bitboard occupancy) const noexcept {
        return bishop_table_[bishop_magics_[square].index(occupancy)];
    }

    [[nodiscard]] Bitboard between(Square from, Square to) const noexcept {
        return between_[from][to];
    }

    [[nodiscard]] Bitboard line(Square from, Square to) const noexcept { return line_[from][to]; }

    [[nodiscard]] const Magic& rook_magic(Square square) const noexcept {
        return rook_magics_[square];
    }

    [[nodiscard]] const Magic& bishop_magic(Square square) const noexcept {
        return bishop_magics_[square];
    }

private:
    std::array<Magic, kSquareCount> rook_magics_{};
    std::array<Magic, kSquareCount> bishop_magics_{};
    std::array<Bitboard, kRookTableSize> rook_table_{};
    std::array<Bitboard, kBishopTableSize> bishop_table_{};
    std::array<std::array<Bitboard, kSquareCount>, kSquareCount> between_{};
    std::array<std::array<Bitboard, kSquareCount>, kSquareCount> line_{};
};

// The process-wide tables, built on first use. Call it once at startup so the magic search
// does not happen in the middle of a search.
[[nodiscard]] inline const SliderTables& slider_tables() {
    static const SliderTables tables;
    return tables;
}

// Reference ray-walking implementation, used to build the tables and to test them.
[[nodiscard]] Bitboard sliding_attacks(PieceType type, Square square, Bitboard occupancy) noexcept;

[[nodiscard]] inline Bitboard rook_attacks(Square square, Bitboard occupancy) {
    return slider_tables().rook(square, occupancy);
}

[[nodiscard]] inline Bitboard bishop_attacks(Square square, Bitboard occupancy) {
    return slider_tables().bishop(square, occupancy);
}

[[nodiscard]] inline Bitboard queen_attacks(Square square, Bitboard occupancy) {
    return rook_attacks(square, occupancy) | bishop_attacks(square, occupancy);
}

// Squares strictly between two aligned squares; empty when they share no line.
[[nodiscard]] inline Bitboard between_bb(Square from, Square to) {
    return slider_tables().between(from, to);
}

// The whole rank, file or diagonal through two squares; empty when they share no line.
[[nodiscard]] inline Bitboard line_bb(Square from, Square to) {
    return slider_tables().line(from, to);
}

// Attacks of a non-pawn piece type.
[[nodiscard]] Bitboard piece_attacks(PieceType type, Square square, Bitboard occupancy);

}  // namespace chess
