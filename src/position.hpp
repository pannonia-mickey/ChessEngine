#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bitboard.hpp"
#include "move.hpp"
#include "types.hpp"

namespace chess {

// A chess position: piece placement plus the game state needed by the rules, and the history
// needed to take moves back.
class Position {
public:
    static constexpr std::string_view kStartFen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    // The standard starting position.
    Position();

    // Parses Forsyth-Edwards Notation. The halfmove and fullmove fields may be omitted.
    // Returns nullopt for malformed input or an impossible position (e.g. missing kings).
    [[nodiscard]] static std::optional<Position> from_fen(std::string_view fen);

    [[nodiscard]] std::string fen() const;

    [[nodiscard]] Piece piece_on(Square square) const noexcept { return board_[square]; }
    [[nodiscard]] Bitboard pieces() const noexcept { return by_color_[White] | by_color_[Black]; }
    [[nodiscard]] Bitboard pieces(Color color) const noexcept { return by_color_[color]; }
    [[nodiscard]] Bitboard pieces(PieceType type) const noexcept { return by_type_[type]; }
    [[nodiscard]] Bitboard pieces(Color color, PieceType type) const noexcept {
        return by_color_[color] & by_type_[type];
    }
    [[nodiscard]] Square king_square(Color color) const noexcept {
        return lsb(pieces(color, King));
    }

    [[nodiscard]] Color side_to_move() const noexcept { return side_to_move_; }
    [[nodiscard]] CastlingRights castling_rights() const noexcept { return castling_; }
    // The en passant target square, set only when a capture onto it is pseudo-legal.
    [[nodiscard]] Square en_passant_square() const noexcept { return en_passant_; }
    [[nodiscard]] int halfmove_clock() const noexcept { return halfmove_clock_; }
    [[nodiscard]] int fullmove_number() const noexcept { return fullmove_number_; }
    [[nodiscard]] Key key() const noexcept { return key_; }

    // All pieces of both colors attacking a square, given an occupancy.
    [[nodiscard]] Bitboard attackers_to(Square square, Bitboard occupancy) const;
    [[nodiscard]] Bitboard attackers_to(Square square) const {
        return attackers_to(square, pieces());
    }

    // Enemy pieces giving check to the side to move.
    [[nodiscard]] Bitboard checkers() const;
    [[nodiscard]] bool in_check() const { return checkers() != 0; }

    // Plays a legal move. Behavior is undefined for moves not generated for this position.
    void make_move(Move move);
    // Takes back the last move made with make_move.
    void unmake_move();

    // Passes the turn to the opponent without moving, as null move pruning wants. Not allowed in
    // check. Repetition detection does not look past a null move.
    void make_null_move();
    // Takes back the last move made with make_null_move.
    void unmake_null_move();

    // True when the current position occurred before with the same side to move since the last
    // capture or pawn move. Search treats a single repetition as a draw.
    [[nodiscard]] bool is_repetition() const noexcept;

    // Number of moves that can be taken back.
    [[nodiscard]] std::size_t ply() const noexcept { return history_.size(); }

    // The Zobrist key computed from scratch; equals key() unless incremental updates are buggy.
    [[nodiscard]] Key compute_key() const;

    // A human-readable board diagram followed by the FEN and key.
    [[nodiscard]] std::string pretty() const;

private:
    // Everything make_move cannot recompute when taking a move back.
    struct UndoInfo {
        Move move;
        Piece captured = NoPiece;
        CastlingRights castling = kNoCastling;
        Square en_passant = NoSquare;
        int halfmove_clock = 0;
        Key key = 0;
    };

    struct EmptyTag {};
    // An empty board with default state, the starting point for FEN parsing.
    explicit Position(EmptyTag tag) noexcept;

    // Sets up the position from FEN on an empty board; false if the FEN is invalid.
    [[nodiscard]] bool parse_fen(std::string_view fen);

    // Board updates. The key is updated too, unless UpdateKey is false: taking a move back
    // restores the key saved before the move instead.
    template <bool UpdateKey = true>
    void put_piece(Piece piece, Square square) noexcept;
    template <bool UpdateKey = true>
    void remove_piece(Square square) noexcept;
    template <bool UpdateKey = true>
    void move_piece(Square from, Square to) noexcept;

    // Sets en_passant_ to the square behind a double-pushed pawn when it can be captured.
    void set_en_passant(Square square) noexcept;
    [[nodiscard]] bool is_consistent() const;

    std::array<Piece, kSquareCount> board_{};
    std::array<Bitboard, kColorCount> by_color_{};
    std::array<Bitboard, kPieceTypeCount> by_type_{};
    Color side_to_move_ = White;
    CastlingRights castling_ = kNoCastling;
    Square en_passant_ = NoSquare;
    int halfmove_clock_ = 0;
    int fullmove_number_ = 1;
    Key key_ = 0;
    std::vector<UndoInfo> history_;
};

}  // namespace chess
