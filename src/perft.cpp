#include "perft.hpp"

#include <cstdint>
#include <vector>

#include "movegen.hpp"

namespace chess {

// NOLINTNEXTLINE(misc-no-recursion): recursion depth is bounded by the requested depth.
std::uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) {
        return 1;
    }
    const MoveList moves = generate_legal_moves(pos);
    if (depth == 1) {
        return moves.size();  // Bulk counting: the moves are legal, so no need to play them.
    }
    std::uint64_t nodes = 0;
    for (const Move move : moves) {
        pos.make_move(move);
        nodes += perft(pos, depth - 1);
        pos.unmake_move();
    }
    return nodes;
}

std::vector<PerftEntry> perft_divide(Position& pos, int depth) {
    std::vector<PerftEntry> entries;
    if (depth <= 0) {
        return entries;
    }
    for (const Move move : generate_legal_moves(pos)) {
        pos.make_move(move);
        entries.push_back({.move = move, .nodes = perft(pos, depth - 1)});
        pos.unmake_move();
    }
    return entries;
}

}  // namespace chess
