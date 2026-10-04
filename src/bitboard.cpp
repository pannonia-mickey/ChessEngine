#include "bitboard.hpp"

#include <string>

namespace chess {

std::string to_string(Bitboard bb) {
    std::string text;
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            text += has(bb, make_square(file, rank)) ? 'X' : '.';
        }
        text += '\n';
    }
    return text;
}

}  // namespace chess
