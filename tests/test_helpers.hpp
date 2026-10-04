#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "move.hpp"
#include "movegen.hpp"
#include "position.hpp"

namespace chess::test {

// Parses a FEN the test relies on being valid; throws (failing the test) otherwise.
inline Position position_from(std::string_view fen) {
    auto pos = Position::from_fen(fen);
    if (!pos.has_value()) {
        throw std::invalid_argument("invalid FEN: " + std::string(fen));
    }
    return *pos;
}

// Finds a move the test relies on being legal; throws (failing the test) otherwise.
inline Move legal_move(const Position& pos, std::string_view text) {
    const auto move = parse_uci_move(pos, text);
    if (!move.has_value()) {
        throw std::invalid_argument("illegal move: " + std::string(text));
    }
    return *move;
}

}  // namespace chess::test
