#pragma once

#include <array>
#include <cstdint>

#include "types.hpp"

namespace chess::zobrist {

struct Keys {
    std::array<std::array<Key, kSquareCount>, kPieceCount> piece_square{};
    std::array<Key, kCastlingCombinations> castling{};
    std::array<Key, 8> en_passant_file{};
    Key side_to_move = 0;
};

namespace detail {

// splitmix64: a fast, well-distributed generator that is easy to evaluate at compile time.
constexpr std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

consteval Keys make_keys() {
    Keys keys;
    std::uint64_t state = 0x5EED'C0FF'EE15'600DULL;
    for (auto& squares : keys.piece_square) {
        for (auto& key : squares) {
            key = splitmix64(state);
        }
    }
    for (auto& key : keys.castling) {
        key = splitmix64(state);
    }
    for (auto& key : keys.en_passant_file) {
        key = splitmix64(state);
    }
    keys.side_to_move = splitmix64(state);
    return keys;
}

}  // namespace detail

// Zobrist keys, generated at compile time.
constexpr Keys kKeys = detail::make_keys();

[[nodiscard]] constexpr Key piece_square(Piece piece, Square square) noexcept {
    return kKeys.piece_square[piece][square];
}

[[nodiscard]] constexpr Key castling(CastlingRights rights) noexcept {
    return kKeys.castling[rights];
}

[[nodiscard]] constexpr Key en_passant(Square square) noexcept {
    return kKeys.en_passant_file[static_cast<std::size_t>(file_of(square))];
}

[[nodiscard]] constexpr Key side_to_move() noexcept {
    return kKeys.side_to_move;
}

}  // namespace chess::zobrist
