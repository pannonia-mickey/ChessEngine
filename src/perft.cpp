#include "perft.hpp"
#include "movegen.hpp"

namespace chess {

std::uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList list; generate_legal(pos, list);
    if (depth == 1) return list.size;
    std::uint64_t nodes = 0;
    StateInfo st;
    for (Move m : list) {
        pos.do_move(m, st);
        nodes += perft(pos, depth - 1);
        pos.undo_move(m, st);
    }
    return nodes;
}

} // namespace chess
