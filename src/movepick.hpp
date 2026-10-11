#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "history.hpp"
#include "move.hpp"
#include "movegen.hpp"
#include "position.hpp"
#include "types.hpp"

namespace chess {

[[nodiscard]] inline bool is_capture(const Position& pos, Move move) {
    return move.type() == MoveType::EnPassant || pos.piece_on(move.to()) != NoPiece;
}

// Neither a capture nor a promotion: the moves ordered by killers and history.
[[nodiscard]] inline bool is_quiet(const Position& pos, Move move) {
    return !is_capture(pos, move) && move.type() != MoveType::Promotion;
}

// Captures and queen promotions: the moves quiescence search looks at.
[[nodiscard]] inline bool is_tactical(const Position& pos, Move move) {
    return is_capture(pos, move) ||
           (move.type() == MoveType::Promotion && move.promotion() == Queen);
}

// The type of the piece a capture takes.
[[nodiscard]] inline PieceType captured_type(const Position& pos, Move move) {
    return move.type() == MoveType::EnPassant ? Pawn : type_of(pos.piece_on(move.to()));
}

// The two most recent quiet moves that caused a beta cutoff at a ply, newest first. Sibling
// positions tend to be refuted by the same move.
using Killers = std::array<Move, 2>;

// Quiet moves are scored by the continuation histories of this many earlier moves.
constexpr std::size_t kContinuationPlies = 2;

// The continuation histories of the moves one and two plies before a node, or null where there
// is no such move (the root, or a null move).
using ContinuationHistories = std::array<const PieceToHistory*, kContinuationPlies>;

// The combined history score of a quiet move: its butterfly history plus its continuation
// histories.
[[nodiscard]] int quiet_history(const Position& pos, Move move, const ButterflyHistory& butterfly,
                                const ContinuationHistories& continuation);

// What a node knows to order its moves by. Every table may be null, then it scores nothing.
struct MoveOrdering {
    Move hash_move = Move::null();
    Killers killers{};
    Move counter_move = Move::null();
    const ButterflyHistory* butterfly = nullptr;
    ContinuationHistories continuation{};
    const CaptureHistory* captures = nullptr;
};

class MovePicker {
public:
    // Orders the hash/PV move first, then captures that do not lose material by static exchange
    // evaluation, by MVV-LVA (most valuable victim, least valuable attacker) and capture history,
    // and queen promotions, then the killer moves, then the counter move, then the other quiet
    // moves by history score, then the losing captures, and underpromotions last. With
    // `tactical_only`, only captures and queen promotions are returned.
    void reset(const Position& pos, const MoveList& moves, const MoveOrdering& ordering,
               bool tactical_only = false);

    // The highest-scored move not returned yet, or nullopt when all have been.
    std::optional<Move> next();

    // see_ge(pos, move, 0) for the move next() returned last, reusing the result from scoring it
    // when there is one.
    [[nodiscard]] bool last_see_ge_zero(const Position& pos);

private:
    enum class SeeResult : std::uint8_t { Unknown, Good, Bad };

    static int score(const Position& pos, Move move, const MoveOrdering& ordering, SeeResult& see);

    std::array<Move, MoveList::kCapacity> moves_{};
    std::array<int, MoveList::kCapacity> scores_{};
    std::array<SeeResult, MoveList::kCapacity> see_{};
    std::size_t size_ = 0;
    std::size_t current_ = 0;
};

}  // namespace chess
