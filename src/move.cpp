#include "move.hpp"

#include <algorithm>
#include <string>

namespace chess {

std::string square_name(Square square) {
    return {static_cast<char>('a' + file_of(square)), static_cast<char>('1' + rank_of(square))};
}

std::string Move::to_uci() const {
    if (is_null()) {
        return "0000";
    }
    std::string text = square_name(from()) + square_name(to());
    if (type() == MoveType::Promotion) {
        text += "nbrq"[promotion() - Knight];
    }
    return text;
}

bool MoveList::contains(Move move) const noexcept {
    return std::ranges::find(*this, move) != end();
}

}  // namespace chess
