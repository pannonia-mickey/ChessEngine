#include "position.hpp"

#include "attacks.hpp"
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace chess {

void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    by_type_[type_of(p)] |= square_bb(s);
    by_color_[color_of(p)] |= square_bb(s);
    key_ ^= zobrist::psq[p][s];
    if (type_of(p) == PAWN) pawn_key_ ^= zobrist::psq[p][s];

    mg_psq_ += PSQ_MG[p][s];
    eg_psq_ += PSQ_EG[p][s];
    phase_ += PHASE_WEIGHT[type_of(p)];
}

void Position::remove_piece(Square s) {
    Piece p = board_[s];
    by_type_[type_of(p)] &= ~square_bb(s);
    by_color_[color_of(p)] &= ~square_bb(s);
    board_[s] = NO_PIECE;
    key_ ^= zobrist::psq[p][s];
    if (type_of(p) == PAWN) pawn_key_ ^= zobrist::psq[p][s];

    mg_psq_ -= PSQ_MG[p][s];
    eg_psq_ -= PSQ_EG[p][s];
    phase_ -= PHASE_WEIGHT[type_of(p)];
}

namespace {

// Rook squares involved in a castling move, keyed by the king's destination.
void castling_rook_squares(Square king_to, Square& rook_from, Square& rook_to) {
    switch (king_to) {
        case SQ_G1: rook_from = SQ_H1; rook_to = SQ_F1; break;
        case SQ_C1: rook_from = SQ_A1; rook_to = SQ_D1; break;
        case SQ_G8: rook_from = SQ_H8; rook_to = SQ_F8; break;
        case SQ_C8: rook_from = SQ_A8; rook_to = SQ_D8; break;
        default: rook_from = SQ_NONE; rook_to = SQ_NONE; break;
    }
}

Piece piece_from_char(char c) {
    Color color = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'p': return make_piece(color, PAWN);
        case 'n': return make_piece(color, KNIGHT);
        case 'b': return make_piece(color, BISHOP);
        case 'r': return make_piece(color, ROOK);
        case 'q': return make_piece(color, QUEEN);
        case 'k': return make_piece(color, KING);
        default: return NO_PIECE;
    }
}

char char_from_piece(Piece p) {
    static const char* symbols = "PNBRQK";
    char c = symbols[type_of(p)];
    return color_of(p) == WHITE ? c : static_cast<char>(std::tolower(c));
}

} // namespace

bool Position::set(const std::string& fen) {
    std::istringstream iss(fen);
    std::string placement, side, castling, ep, halfmove, fullmove;
    iss >> placement >> side >> castling >> ep >> halfmove >> fullmove;

    // Validate and parse into local state first; only commit to the
    // Position's members once the whole FEN is known to be well-formed, so a
    // malformed FEN leaves this object untouched rather than corrupting it.

    // Piece placement: ranks 8 -> 1, files a -> h within each rank. Bounds
    // are checked before every placement so a malformed field (extra '/'
    // separators, or too many file-units on a rank) is rejected instead of
    // indexing board_[] out of range.
    Piece new_board[SQUARE_NB];
    for (int s = 0; s < SQUARE_NB; ++s) new_board[s] = NO_PIECE;

    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            --rank;
            file = 0;
            if (rank < 0) return false;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            file += c - '0';
            if (file > 8) return false;
        } else {
            if (rank < 0 || rank > 7 || file < 0 || file > 7) return false;
            Piece pc = piece_from_char(c);
            // A pawn can never legally sit on the back rank (it would have
            // had to promote), so reject a FEN that places one there instead
            // of silently letting it reach pawn-push logic that assumes
            // otherwise.
            if (type_of(pc) == PAWN && (rank == 0 || rank == 7)) return false;
            new_board[make_square(file, rank)] = pc;
            ++file;
        }
    }

    // Halfmove/fullmove: exception-free parsing via std::from_chars. Any
    // non-numeric, overflowing, or trailing-garbage field is rejected.
    int halfmove_val = 0, fullmove_val = 1;
    if (!halfmove.empty()) {
        auto res = std::from_chars(halfmove.data(), halfmove.data() + halfmove.size(), halfmove_val);
        if (res.ec != std::errc() || res.ptr != halfmove.data() + halfmove.size()) return false;
    }
    if (!fullmove.empty()) {
        auto res = std::from_chars(fullmove.data(), fullmove.data() + fullmove.size(), fullmove_val);
        if (res.ec != std::errc() || res.ptr != fullmove.data() + fullmove.size()) return false;
    }

    // Everything validated; commit to member state. Keep a snapshot first
    // so we can roll back if the fully-parsed FEN turns out to be illegal
    // (the side not to move left in check) — the earlier failure paths all
    // return before this point specifically so a malformed FEN never
    // touches member state; this snapshot extends that same guarantee to a
    // failure that can only be detected after piece placement is known.
    Position saved = *this;

    for (int t = 0; t < PIECE_TYPE_NB; ++t) by_type_[t] = 0;
    for (int c = 0; c < COLOR_NB; ++c) by_color_[c] = 0;
    for (int s = 0; s < SQUARE_NB; ++s) board_[s] = NO_PIECE;
    key_ = 0;
    pawn_key_ = 0;
    mg_psq_ = 0;
    eg_psq_ = 0;
    phase_ = 0;
    for (int s = 0; s < SQUARE_NB; ++s) {
        if (new_board[s] != NO_PIECE) put_piece(new_board[s], Square(s));
    }

    stm_ = (side == "b") ? BLACK : WHITE;

    // Castling rights are only granted when the king and rook they refer to
    // are actually on their home squares. A right claimed for a rook that
    // isn't there would later let do_move()'s CASTLING branch remove/put a
    // NO_PIECE, corrupting by_type_/by_color_ (color_of/type_of assume a
    // real piece).
    castling_ = 0;
    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
                case 'K':
                    if (new_board[SQ_E1] == W_KING && new_board[SQ_H1] == W_ROOK)
                        castling_ |= WHITE_OO;
                    break;
                case 'Q':
                    if (new_board[SQ_E1] == W_KING && new_board[SQ_A1] == W_ROOK)
                        castling_ |= WHITE_OOO;
                    break;
                case 'k':
                    if (new_board[SQ_E8] == B_KING && new_board[SQ_H8] == B_ROOK)
                        castling_ |= BLACK_OO;
                    break;
                case 'q':
                    if (new_board[SQ_E8] == B_KING && new_board[SQ_A8] == B_ROOK)
                        castling_ |= BLACK_OOO;
                    break;
                default: break;
            }
        }
    }

    // Likewise, an en-passant square is only real if the side to move's
    // opponent could have just made the double push it implies: a black
    // pawn behind rank 6 (side to move white) or a white pawn behind rank 3
    // (side to move black). Otherwise do_move()'s EN_PASSANT branch would
    // remove/put a NO_PIECE at the (nonexistent) captured pawn's square.
    ep_ = SQ_NONE;
    if (ep != "-" && ep.size() >= 2) {
        int ep_file = ep[0] - 'a';
        int ep_rank = ep[1] - '1';
        if (ep_file >= 0 && ep_file <= 7 && ep_rank >= 0 && ep_rank <= 7) {
            bool valid =
                (stm_ == WHITE && ep_rank == 5 &&
                 new_board[make_square(ep_file, 4)] == B_PAWN) ||
                (stm_ == BLACK && ep_rank == 2 &&
                 new_board[make_square(ep_file, 3)] == W_PAWN);
            if (valid) ep_ = make_square(ep_file, ep_rank);
        }
    }

    halfmove_ = halfmove_val;
    fullmove_ = fullmove_val;

    key_ ^= zobrist::castling[castling_];
    if (ep_ != SQ_NONE) key_ ^= zobrist::ep_file[file_of(ep_)];
    if (stm_ == BLACK) key_ ^= zobrist::side;

    // A FEN is illegal if the side NOT to move is left in check: that could
    // only arise if the side to move's last move ignored or walked into
    // check on its own king, which no legal sequence of moves produces.
    // Skip the check when the opponent doesn't have exactly one king on the
    // board: king_square() assumes exactly one, and some callers (e.g.
    // move-generation tests exercising a single side in isolation)
    // deliberately build boards without a king for one color.
    Color opponent = Color(stm_ ^ 1);
    if (popcount(pieces(opponent, KING)) == 1 &&
        square_attacked_by(king_square(opponent), stm_)) {
        *this = saved;
        return false;
    }

    return true;
}

std::string Position::fen() const {
    std::ostringstream oss;

    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            Square s = make_square(file, rank);
            Piece p = board_[s];
            if (p == NO_PIECE) {
                ++empty;
            } else {
                if (empty > 0) {
                    oss << empty;
                    empty = 0;
                }
                oss << char_from_piece(p);
            }
        }
        if (empty > 0) oss << empty;
        if (rank > 0) oss << '/';
    }

    oss << ' ' << (stm_ == WHITE ? 'w' : 'b') << ' ';

    if (castling_ == 0) {
        oss << '-';
    } else {
        if (castling_ & WHITE_OO) oss << 'K';
        if (castling_ & WHITE_OOO) oss << 'Q';
        if (castling_ & BLACK_OO) oss << 'k';
        if (castling_ & BLACK_OOO) oss << 'q';
    }

    oss << ' ';
    if (ep_ == SQ_NONE) {
        oss << '-';
    } else {
        oss << static_cast<char>('a' + file_of(ep_)) << static_cast<char>('1' + rank_of(ep_));
    }

    oss << ' ' << halfmove_ << ' ' << fullmove_;

    return oss.str();
}

bool Position::square_attacked_by(Square s, Color by) const {
    return square_attacked_by(s, by, occupied());
}

bool Position::square_attacked_by(Square s, Color by, Bitboard occ) const {
    // Reverse trick: a pawn of color `by` attacks `s` from the squares that
    // a (hypothetical) opposite-colored pawn on `s` would attack.
    Color opponent = Color(by ^ 1);
    if (pawn_attacks(opponent, s) & pieces(by, PAWN)) return true;
    if (knight_attacks(s) & pieces(by, KNIGHT)) return true;
    if (king_attacks(s) & pieces(by, KING)) return true;
    if (bishop_attacks(s, occ) & (pieces(by, BISHOP) | pieces(by, QUEEN))) return true;
    if (rook_attacks(s, occ) & (pieces(by, ROOK) | pieces(by, QUEEN))) return true;
    return false;
}

Bitboard Position::attackers_to(Square s, Color by) const {
    Color opponent = Color(by ^ 1);
    Bitboard occ = occupied();
    Bitboard att = 0;
    att |= pawn_attacks(opponent, s) & pieces(by, PAWN);
    att |= knight_attacks(s) & pieces(by, KNIGHT);
    att |= king_attacks(s) & pieces(by, KING);
    att |= bishop_attacks(s, occ) & (pieces(by, BISHOP) | pieces(by, QUEEN));
    att |= rook_attacks(s, occ) & (pieces(by, ROOK) | pieces(by, QUEEN));
    return att;
}

void Position::do_move(Move m, StateInfo& st) {
    Color us = stm_;
    Square from = from_sq(m);
    Square to = to_sq(m);
    MoveFlag flag = flag_of(m);
    Piece moving = piece_on(from);
    PieceType moving_type = type_of(moving);

    st.castling = castling_;
    st.ep = ep_;
    st.halfmove = halfmove_;
    st.captured = NO_PIECE;

    bool is_capture = false;
    Square ep_after = SQ_NONE;

    switch (flag) {
        case NORMAL: {
            Piece captured = piece_on(to);
            if (captured != NO_PIECE) {
                st.captured = captured;
                is_capture = true;
                remove_piece(to);
            }
            remove_piece(from);
            put_piece(moving, to);

            if (moving_type == PAWN && std::abs(rank_of(to) - rank_of(from)) == 2) {
                ep_after = make_square(file_of(from), (rank_of(from) + rank_of(to)) / 2);
            }
            break;
        }
        case PROMOTION: {
            Piece captured = piece_on(to);
            if (captured != NO_PIECE) {
                st.captured = captured;
                is_capture = true;
                remove_piece(to);
            }
            remove_piece(from);
            put_piece(make_piece(us, promo_type(m)), to);
            break;
        }
        case EN_PASSANT: {
            Square captured_sq = make_square(file_of(to), rank_of(from));
            st.captured = piece_on(captured_sq);
            is_capture = true;
            remove_piece(captured_sq);
            remove_piece(from);
            put_piece(moving, to);
            break;
        }
        case CASTLING: {
            remove_piece(from);
            put_piece(moving, to);
            Square rook_from, rook_to;
            castling_rook_squares(to, rook_from, rook_to);
            Piece rook = piece_on(rook_from);
            remove_piece(rook_from);
            put_piece(rook, rook_to);
            break;
        }
    }

    // Update castling rights: clearing bits is safe/idempotent even when the
    // relevant right was already lost. Triggered by the king or rook leaving
    // its home square, or a rook's home square being captured on.
    key_ ^= zobrist::castling[castling_]; // remove hash for the old rights
    if (from == SQ_E1) castling_ &= ~(WHITE_OO | WHITE_OOO);
    if (from == SQ_E8) castling_ &= ~(BLACK_OO | BLACK_OOO);
    if (from == SQ_A1 || to == SQ_A1) castling_ &= ~WHITE_OOO;
    if (from == SQ_H1 || to == SQ_H1) castling_ &= ~WHITE_OO;
    if (from == SQ_A8 || to == SQ_A8) castling_ &= ~BLACK_OOO;
    if (from == SQ_H8 || to == SQ_H8) castling_ &= ~BLACK_OO;
    key_ ^= zobrist::castling[castling_]; // add hash for the new rights

    if (ep_ != SQ_NONE) key_ ^= zobrist::ep_file[file_of(ep_)];
    ep_ = ep_after;
    if (ep_ != SQ_NONE) key_ ^= zobrist::ep_file[file_of(ep_)];

    if (moving_type == PAWN || is_capture) halfmove_ = 0;
    else ++halfmove_;

    if (us == BLACK) ++fullmove_;

    key_ ^= zobrist::side;
    stm_ = Color(us ^ 1);
}

void Position::undo_move(Move m, const StateInfo& st) {
    stm_ = Color(stm_ ^ 1);
    Color us = stm_;

    Square from = from_sq(m);
    Square to = to_sq(m);
    MoveFlag flag = flag_of(m);

    switch (flag) {
        case NORMAL: {
            Piece moved = piece_on(to);
            remove_piece(to);
            put_piece(moved, from);
            if (st.captured != NO_PIECE) put_piece(st.captured, to);
            break;
        }
        case PROMOTION: {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), from);
            if (st.captured != NO_PIECE) put_piece(st.captured, to);
            break;
        }
        case EN_PASSANT: {
            Piece moved = piece_on(to);
            remove_piece(to);
            put_piece(moved, from);
            Square captured_sq = make_square(file_of(to), rank_of(from));
            put_piece(st.captured, captured_sq);
            break;
        }
        case CASTLING: {
            Piece king = piece_on(to);
            remove_piece(to);
            put_piece(king, from);
            Square rook_from, rook_to;
            castling_rook_squares(to, rook_from, rook_to);
            Piece rook = piece_on(rook_to);
            remove_piece(rook_to);
            put_piece(rook, rook_from);
            break;
        }
    }

    // XOR is commutative/associative, so XOR-ing out the current value and
    // XOR-ing in the restored value (in either order) exactly cancels the
    // deltas do_move applied, regardless of the order do_move applied them in.
    key_ ^= zobrist::side;
    key_ ^= zobrist::castling[castling_];
    key_ ^= zobrist::castling[st.castling];
    if (ep_ != SQ_NONE) key_ ^= zobrist::ep_file[file_of(ep_)];
    if (st.ep != SQ_NONE) key_ ^= zobrist::ep_file[file_of(st.ep)];

    castling_ = st.castling;
    ep_ = st.ep;
    halfmove_ = st.halfmove;
    if (us == BLACK) --fullmove_;
}

void Position::do_null_move(NullMoveState& st) {
    st.ep = ep_;
    if (ep_ != SQ_NONE) key_ ^= zobrist::ep_file[file_of(ep_)];
    ep_ = SQ_NONE;
    stm_ = Color(stm_ ^ 1);
    key_ ^= zobrist::side;
}

void Position::undo_null_move(const NullMoveState& st) {
    stm_ = Color(stm_ ^ 1);
    key_ ^= zobrist::side;
    if (st.ep != SQ_NONE) key_ ^= zobrist::ep_file[file_of(st.ep)];
    ep_ = st.ep;
}

} // namespace chess
