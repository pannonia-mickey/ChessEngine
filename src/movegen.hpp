#pragma once

#include <optional>
#include <string_view>

#include "move.hpp"
#include "position.hpp"

namespace chess {

// All strictly legal moves in the position.
[[nodiscard]] MoveList generate_legal_moves(const Position& pos);

// Finds the legal move written in UCI long algebraic notation (e.g. "e7e8q"), if any.
[[nodiscard]] std::optional<Move> parse_uci_move(const Position& pos, std::string_view text);

}  // namespace chess
