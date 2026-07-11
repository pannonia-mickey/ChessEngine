#pragma once

#include "bitboard.hpp"
#include "move.hpp"
#include "types.hpp"
#include "zobrist.hpp"
#include <string>

namespace chess {

// Castling rights bitmask values.
enum CastlingRight : int { WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8 };

// Snapshot of the irreversible parts of position state, saved by do_move()
// so undo_move() can restore them exactly.
struct StateInfo {
    int castling;
    Square ep;
    int halfmove;
    Piece captured;
};

struct NullMoveState {
    Square ep;
};

class Position {
public:
    Position() {
        for (int s = 0; s < SQUARE_NB; ++s) board_[s] = NO_PIECE;
    }

    // Parse a FEN string and set the position's state accordingly.
    // Returns true if `fen` was well-formed and the position was updated, or
    // false if `fen` was malformed (out-of-range ranks/files in the piece
    // placement field, or non-numeric/overflowing halfmove/fullmove fields).
    // On false, the position's state is left unchanged (validation happens
    // before any member state is mutated).
    bool set(const std::string& fen);

    // Serialize the current state back to a FEN string.
    std::string fen() const;

    Bitboard pieces(Color c, PieceType pt) const { return by_color_[c] & by_type_[pt]; }
    Bitboard pieces(Color c) const { return by_color_[c]; }
    Bitboard pieces(PieceType pt) const { return by_type_[pt]; }
    Bitboard occupied() const { return by_color_[WHITE] | by_color_[BLACK]; }

    Piece piece_on(Square s) const { return board_[s]; }
    Color side_to_move() const { return stm_; }
    Square ep_square() const { return ep_; }
    int castling_rights() const { return castling_; }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }
    zobrist::Key key() const { return key_; }

    // Is square `s` attacked by any piece of color `by`, given current occupancy?
    bool square_attacked_by(Square s, Color by) const;

    // Apply move `m`, saving the state needed to undo it into `st`.
    void do_move(Move m, StateInfo& st);
    // Reverse a previously-applied move `m`, restoring state from `st`.
    void undo_move(Move m, const StateInfo& st);

    // Apply/undo a "null move": pass the turn without moving a piece. Used
    // only by null-move pruning in the search. The en passant right always
    // lapses on a null move (it only exists for the immediately-following
    // reply), so it is cleared, not preserved.
    void do_null_move(NullMoveState& st);
    void undo_null_move(const NullMoveState& st);

private:
    void put_piece(Piece p, Square s);
    void remove_piece(Square s);

    Bitboard by_type_[PIECE_TYPE_NB] = {};
    Bitboard by_color_[COLOR_NB] = {};
    Piece board_[SQUARE_NB];
    Color stm_ = WHITE;
    int castling_ = 0;
    Square ep_ = SQ_NONE;
    int halfmove_ = 0;
    int fullmove_ = 1;
    zobrist::Key key_ = 0;
};

} // namespace chess
