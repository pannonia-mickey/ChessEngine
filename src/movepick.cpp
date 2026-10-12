#include "movepick.hpp"

#include <utility>

#include "evaluate.hpp"
#include "see.hpp"

namespace chess {

int quiet_history(const Position& pos, Move move, const ButterflyHistory& butterfly,
                  const ContinuationHistories& continuation) {
    const Piece piece = pos.piece_on(move.from());
    int value = butterfly.get(pos.side_to_move(), move);
    for (const PieceToHistory* history : continuation) {
        if (history != nullptr) {
            value += history->get(piece, move.to());
        }
    }
    return value;
}

void MovePicker::reset(const Position& pos, const MoveList& moves, const MoveOrdering& ordering,
                       bool tactical_only) {
    size_ = 0;
    current_ = 0;
    for (const Move move : moves) {
        if (tactical_only && !is_tactical(pos, move)) {
            continue;
        }
        moves_[size_] = move;
        see_[size_] = SeeResult::Unknown;
        scores_[size_] = score(pos, move, ordering, see_[size_]);
        ++size_;
    }
}

std::optional<Move> MovePicker::next() {
    if (current_ == size_) {
        return std::nullopt;
    }
    std::size_t best = current_;
    for (std::size_t i = current_ + 1; i < size_; ++i) {
        if (scores_[i] > scores_[best]) {
            best = i;
        }
    }
    std::swap(moves_[current_], moves_[best]);
    std::swap(scores_[current_], scores_[best]);
    std::swap(see_[current_], see_[best]);
    return moves_[current_++];
}

bool MovePicker::last_see_ge_zero(const Position& pos) {
    SeeResult& see = see_[current_ - 1];
    if (see == SeeResult::Unknown) {
        see = see_ge(pos, moves_[current_ - 1], 0) ? SeeResult::Good : SeeResult::Bad;
    }
    return see == SeeResult::Good;
}

int MovePicker::score(const Position& pos, Move move, const MoveOrdering& ordering,
                      SeeResult& see) {
    constexpr int kHashBonus = 1'000'000;
    constexpr int kTacticalBonus = 100'000;
    constexpr int kFirstKillerBonus = 90'000;
    constexpr int kSecondKillerBonus = 80'000;
    constexpr int kCounterMoveBonus = 70'000;
    constexpr int kLosingCaptureScore = -kTacticalBonus;
    constexpr int kUnderpromotionScore = -2 * kTacticalBonus;
    // Scaled down this far, capture history mostly reorders captures of the same victim, and the
    // victim's value still decides between the others.
    constexpr int kCaptureHistoryDivisor = 16;
    // The killer and counter move bonuses must outrank every history score.
    static_assert(kCounterMoveBonus > 3 * kMaxHistory);

    if (move == ordering.hash_move) {
        return kHashBonus;
    }
    // Promoting to anything but a queen rarely helps, so it is tried after every quiet move.
    if (move.type() == MoveType::Promotion && move.promotion() != Queen) {
        return kUnderpromotionScore;
    }
    int value = 0;
    if (is_capture(pos, move)) {
        const Piece piece = pos.piece_on(move.from());
        const PieceType victim = captured_type(pos, move);
        int capture_score = (piece_value(victim) * 8) - static_cast<int>(type_of(piece));
        if (ordering.captures != nullptr) {
            capture_score +=
                ordering.captures->get(piece, move.to(), victim) / kCaptureHistoryDivisor;
        }
        // A capture that loses material is tried after the quiet moves.
        see = see_ge(pos, move, 0) ? SeeResult::Good : SeeResult::Bad;
        if (see == SeeResult::Bad) {
            return kLosingCaptureScore + capture_score;
        }
        value += kTacticalBonus + capture_score;
    }
    if (move.type() == MoveType::Promotion) {
        value += kTacticalBonus + piece_value(Queen);
    }
    if (value != 0) {
        return value;
    }
    if (move == ordering.killers[0]) {
        return kFirstKillerBonus;
    }
    if (move == ordering.killers[1]) {
        return kSecondKillerBonus;
    }
    if (move == ordering.counter_move) {
        return kCounterMoveBonus;
    }
    return ordering.butterfly != nullptr
               ? quiet_history(pos, move, *ordering.butterfly, ordering.continuation)
               : 0;
}

}  // namespace chess
