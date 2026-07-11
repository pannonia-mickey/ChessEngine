#include "bitboard.hpp"
#include <sstream>

namespace chess {

std::string pretty(Bitboard b) {
    std::ostringstream oss;

    // Print from rank 8 down to rank 1 (top to bottom)
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            Square sq = make_square(file, rank);
            if (b & square_bb(sq)) {
                oss << "X ";
            } else {
                oss << ". ";
            }
        }
        oss << "\n";
    }

    return oss.str();
}

} // namespace chess
