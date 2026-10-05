#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "types.hpp"

namespace chess {

enum class MoveType : std::uint8_t { Normal, Promotion, EnPassant, Castling };

// A move packed into 16 bits: from (6) | to (6) | promotion piece (2) | move type (2).
// Castling is encoded as the king's two-square move (e1g1), matching UCI notation.
class Move {
public:
    constexpr Move() noexcept = default;

    constexpr Move(Square from, Square to, MoveType type = MoveType::Normal,
                   PieceType promotion = Knight) noexcept
        : data_(static_cast<std::uint16_t>(static_cast<unsigned>(from) |
                                           (static_cast<unsigned>(to) << 6U) |
                                           (static_cast<unsigned>(promotion - Knight) << 12U) |
                                           (static_cast<unsigned>(type) << 14U))) {}

    [[nodiscard]] static constexpr Move null() noexcept { return {}; }

    // The move whose raw() is `data`.
    [[nodiscard]] static constexpr Move from_raw(std::uint16_t data) noexcept {
        Move move;
        move.data_ = data;
        return move;
    }

    [[nodiscard]] constexpr Square from() const noexcept {
        return static_cast<Square>(data_ & 0x3FU);
    }

    [[nodiscard]] constexpr Square to() const noexcept {
        return static_cast<Square>((data_ >> 6) & 0x3FU);
    }

    [[nodiscard]] constexpr MoveType type() const noexcept {
        return static_cast<MoveType>(data_ >> 14);
    }

    // Only meaningful for MoveType::Promotion.
    [[nodiscard]] constexpr PieceType promotion() const noexcept {
        return static_cast<PieceType>(((data_ >> 12) & 0x3U) + Knight);
    }

    [[nodiscard]] constexpr bool is_null() const noexcept { return data_ == 0; }

    [[nodiscard]] constexpr std::uint16_t raw() const noexcept { return data_; }

    constexpr bool operator==(const Move&) const noexcept = default;

    // Long algebraic notation as used by UCI, e.g. "e2e4" or "e7e8q". The null move is "0000".
    [[nodiscard]] std::string to_uci() const;

private:
    std::uint16_t data_ = 0;
};

[[nodiscard]] std::string square_name(Square square);

// A fixed-capacity move container; no position has more than 218 legal moves.
class MoveList {
public:
    static constexpr std::size_t kCapacity = 256;

    constexpr void push_back(Move move) noexcept { moves_[size_++] = move; }

    constexpr void clear() noexcept { size_ = 0; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] constexpr const Move& operator[](std::size_t index) const noexcept {
        return moves_[index];
    }

    [[nodiscard]] constexpr std::span<const Move> moves() const noexcept {
        return std::span<const Move>(moves_).first(size_);
    }

    [[nodiscard]] constexpr auto begin() const noexcept { return moves_.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept {
        return moves_.begin() + static_cast<std::ptrdiff_t>(size_);
    }

    [[nodiscard]] bool contains(Move move) const noexcept;

private:
    std::array<Move, kCapacity> moves_{};
    std::size_t size_ = 0;
};

}  // namespace chess
