#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

#include "move.hpp"
#include "types.hpp"

namespace chess {

// The statistics the search gathers about which moves cause beta cutoffs, used to order moves.
// Every score stays within [-kMaxHistory, kMaxHistory].
constexpr int kMaxHistory = 16'384;

namespace detail {

// Adds `bonus` (negative for a penalty), scaled down as the entry nears kMaxHistory so that
// entries stay bounded and recent results outweigh old ones.
inline void update_history_entry(std::int16_t& entry, int bonus) {
    const int clamped = std::clamp(bonus, -kMaxHistory, kMaxHistory);
    const int value = entry + clamped - (entry * std::abs(clamped) / kMaxHistory);
    entry = static_cast<std::int16_t>(value);
}

}  // namespace detail

// How often each quiet move (by side to move, from and to square) caused a beta cutoff, weighted
// by depth and decreased when it failed to.
class ButterflyHistory {
public:
    [[nodiscard]] int get(Color us, Move move) const { return table_[us][move.from()][move.to()]; }

    void update(Color us, Move move, int bonus) {
        detail::update_history_entry(table_[us][move.from()][move.to()], bonus);
    }

private:
    std::array<std::array<std::array<std::int16_t, kSquareCount>, kSquareCount>, kColorCount>
        table_{};
};

// Scores of quiet moves by moved piece and target square, all following one earlier move.
class PieceToHistory {
public:
    [[nodiscard]] int get(Piece piece, Square to) const { return table_[piece][to]; }

    void update(Piece piece, Square to, int bonus) {
        detail::update_history_entry(table_[piece][to], bonus);
    }

private:
    std::array<std::array<std::int16_t, kSquareCount>, kPieceCount> table_{};
};

// Continuation history: how well a quiet move did as an answer to an earlier move, by the earlier
// move's piece and target square and the answer's. The search keeps a table one ply back (the
// opponent's move) and two plies back (our own previous move). Moves that work as a plan go
// together, and the pair says more than either move alone.
class ContinuationHistory {
public:
    [[nodiscard]] PieceToHistory& entry(Piece piece, Square to) { return table_[piece][to]; }

private:
    std::array<std::array<PieceToHistory, kSquareCount>, kPieceCount> table_{};
};

// How often each capture (by moving piece, target square and captured piece type) caused a beta
// cutoff, to order captures of equal victims.
class CaptureHistory {
public:
    [[nodiscard]] int get(Piece piece, Square to, PieceType captured) const {
        return table_[piece][to][captured];
    }

    void update(Piece piece, Square to, PieceType captured, int bonus) {
        detail::update_history_entry(table_[piece][to][captured], bonus);
    }

private:
    std::array<std::array<std::array<std::int16_t, kPieceTypeCount>, kSquareCount>, kPieceCount>
        table_{};
};

// The quiet move that last refuted each move, by the refuted move's piece and target square: the
// natural reply to a move often does not depend on where it was played from.
class CounterMoves {
public:
    [[nodiscard]] Move get(Piece piece, Square to) const { return table_[piece][to]; }

    void set(Piece piece, Square to, Move move) { table_[piece][to] = move; }

private:
    std::array<std::array<Move, kSquareCount>, kPieceCount> table_{};
};

}  // namespace chess
