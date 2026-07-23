#pragma once

#include "bitboard.hpp"
#include "eval_tables.hpp"
#include "move.hpp"
#include "types.hpp"
#include "zobrist.hpp"
#include <algorithm>
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
    // Returns true if `fen` was well-formed and legal and the position was
    // updated, or false if `fen` was malformed (out-of-range ranks/files in
    // the piece placement field, or non-numeric/overflowing halfmove/fullmove
    // fields) or illegal (the side not to move is in check). On false, the
    // position's state is left unchanged: malformed input is caught before
    // any member state is mutated, while the illegal-position check needs
    // the committed board to evaluate, so that path mutates and then rolls
    // back to a snapshot taken just before committing.
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
    int halfmove() const { return halfmove_; }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }
    zobrist::Key key() const { return key_; }

    // Zobrist key of just the pawns (same zobrist::psq table as key(), but
    // XORed only for PAWN pieces), kept incrementally up to date by
    // put_piece()/remove_piece() alongside key_. A pawn structure's
    // isolated/doubled/backward/passed-pawn score never depends on any
    // non-pawn piece, so this key lets src/pawn_eval.cpp's pawn-structure
    // scoring cache results across positions that share the same pawn
    // layout.
    zobrist::Key pawn_key() const { return pawn_key_; }

    // Incrementally-maintained material+PST sums (White minus Black, before
    // MG/EG taper) and game-phase weight, kept up to date by put_piece()/
    // remove_piece() so eval.cpp doesn't have to rescan every piece on the
    // board on every call. phase() clamps to PHASE_MAX the same way the
    // from-scratch computation did (relevant only for promotion-heavy
    // positions with more non-pawn material than either side starts with).
    int mg_psq() const { return mg_psq_; }
    int eg_psq() const { return eg_psq_; }
    int phase() const { return std::min(phase_, PHASE_MAX); }

    // Is square `s` attacked by any piece of color `by`, given current occupancy?
    bool square_attacked_by(Square s, Color by) const;
    // Overload against a caller-supplied occupancy instead of occupied() -
    // used to test king-move legality without mutating the board first (see
    // generate_legal() in movegen.cpp): pass occupied() with the king's own
    // origin square cleared, so a slider it would otherwise block is seen
    // correctly attacking the destination.
    bool square_attacked_by(Square s, Color by, Bitboard occ) const;

    // All pieces of color `by` that attack square `s`, given current
    // occupancy. Unlike square_attacked_by (which short-circuits on the
    // first hit), this returns the full attacker set - used by move
    // generation to find checkers/pinners without a boolean-only answer.
    Bitboard attackers_to(Square s, Color by) const;

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
    zobrist::Key pawn_key_ = 0;
    int mg_psq_ = 0;
    int eg_psq_ = 0;
    int phase_ = 0;
};

} // namespace chess
