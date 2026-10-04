#pragma once

#include <cstdint>

namespace chess {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;

enum Color : std::uint8_t { White, Black };

constexpr int kColorCount = 2;

[[nodiscard]] constexpr Color operator~(Color color) noexcept {
    return color == White ? Black : White;
}

enum PieceType : std::uint8_t { Pawn, Knight, Bishop, Rook, Queen, King, NoPieceType };

constexpr int kPieceTypeCount = 6;

// Pieces are numbered color * 6 + type, so they index a 12-entry table directly.
enum Piece : std::uint8_t {
    WhitePawn,
    WhiteKnight,
    WhiteBishop,
    WhiteRook,
    WhiteQueen,
    WhiteKing,
    BlackPawn,
    BlackKnight,
    BlackBishop,
    BlackRook,
    BlackQueen,
    BlackKing,
    NoPiece
};

constexpr int kPieceCount = 12;

[[nodiscard]] constexpr Piece make_piece(Color color, PieceType type) noexcept {
    return static_cast<Piece>((color * kPieceTypeCount) + type);
}

[[nodiscard]] constexpr Color color_of(Piece piece) noexcept {
    return piece >= BlackPawn ? Black : White;
}

[[nodiscard]] constexpr PieceType type_of(Piece piece) noexcept {
    return piece == NoPiece ? NoPieceType : static_cast<PieceType>(piece % kPieceTypeCount);
}

// clang-format off
enum Square : std::uint8_t {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NoSquare
};
// clang-format on

constexpr int kSquareCount = 64;

[[nodiscard]] constexpr Square make_square(int file, int rank) noexcept {
    return static_cast<Square>((rank * 8) + file);
}

[[nodiscard]] constexpr int file_of(Square square) noexcept {
    return square & 7;
}

[[nodiscard]] constexpr int rank_of(Square square) noexcept {
    return square >> 3;
}

[[nodiscard]] constexpr bool is_valid(Square square) noexcept {
    return square < NoSquare;
}

// Offsets a square by the given number of squares; the caller guarantees the result is on board.
[[nodiscard]] constexpr Square offset(Square square, int delta) noexcept {
    return static_cast<Square>(square + delta);
}

// Mirrors a square vertically (a1 <-> a8) to express rules from the side to move's view.
[[nodiscard]] constexpr Square relative_square(Color color, Square square) noexcept {
    return color == White ? square : static_cast<Square>(square ^ 56);
}

[[nodiscard]] constexpr int relative_rank(Color color, Square square) noexcept {
    return color == White ? rank_of(square) : 7 - rank_of(square);
}

// One step towards the opponent's back rank.
[[nodiscard]] constexpr int pawn_push(Color color) noexcept {
    return color == White ? 8 : -8;
}

// Castling rights as a 4-bit set.
using CastlingRights = std::uint8_t;

constexpr CastlingRights kNoCastling = 0;
constexpr CastlingRights kWhiteKingSide = 1;
constexpr CastlingRights kWhiteQueenSide = 2;
constexpr CastlingRights kBlackKingSide = 4;
constexpr CastlingRights kBlackQueenSide = 8;
constexpr CastlingRights kAllCastling = 15;

constexpr int kCastlingCombinations = 16;

}  // namespace chess
