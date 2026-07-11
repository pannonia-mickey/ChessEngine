#include "movegen.hpp"

#include "attacks.hpp"
#include "bitboard.hpp"

namespace chess {

namespace {

// Emit a pawn move to `to`, expanding to all four promotion pieces when `to`
// lands on the back rank.
void add_pawn_move(MoveList& list, Square from, Square to, bool is_ep = false) {
    if (rank_of(to) == 0 || rank_of(to) == 7) {
        list.add(make_move(from, to, PROMOTION, QUEEN));
        list.add(make_move(from, to, PROMOTION, ROOK));
        list.add(make_move(from, to, PROMOTION, BISHOP));
        list.add(make_move(from, to, PROMOTION, KNIGHT));
    } else if (is_ep) {
        list.add(make_move(from, to, EN_PASSANT));
    } else {
        list.add(make_move(from, to));
    }
}

void generate_pawn_moves(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    Bitboard pawns = pos.pieces(us, PAWN);
    Bitboard enemy = pos.pieces(Color(us ^ 1));
    Bitboard occ = pos.occupied();

    int dir = (us == WHITE) ? 8 : -8;
    Bitboard start_rank = (us == WHITE) ? RANK_2_BB : RANK_7_BB;

    Bitboard bb = pawns;
    while (bb) {
        Square from = pop_lsb(bb);

        // Single and double pushes (only into empty squares).
        Square one = Square(int(from) + dir);
        if (!(occ & square_bb(one))) {
            add_pawn_move(list, from, one);
            if (square_bb(from) & start_rank) {
                Square two = Square(int(from) + 2 * dir);
                if (!(occ & square_bb(two))) list.add(make_move(from, two));
            }
        }

        // Captures (including en passant).
        Bitboard attacks = pawn_attacks(us, from);
        Bitboard captures = attacks & enemy;
        while (captures) {
            Square to = pop_lsb(captures);
            add_pawn_move(list, from, to);
        }
        if (pos.ep_square() != SQ_NONE && (attacks & square_bb(pos.ep_square()))) {
            add_pawn_move(list, from, pos.ep_square(), true);
        }
    }
}

void generate_piece_moves(const Position& pos, PieceType pt, MoveList& list) {
    Color us = pos.side_to_move();
    Bitboard own = pos.pieces(us);
    Bitboard occ = pos.occupied();

    Bitboard bb = pos.pieces(us, pt);
    while (bb) {
        Square from = pop_lsb(bb);
        Bitboard targets;
        switch (pt) {
            case KNIGHT: targets = knight_attacks(from); break;
            case BISHOP: targets = bishop_attacks(from, occ); break;
            case ROOK:   targets = rook_attacks(from, occ); break;
            case QUEEN:  targets = queen_attacks(from, occ); break;
            case KING:   targets = king_attacks(from); break;
            default:     targets = 0; break;
        }
        targets &= ~own;
        while (targets) {
            Square to = pop_lsb(targets);
            list.add(make_move(from, to));
        }
    }
}

void generate_castling_moves(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    int rights = pos.castling_rights();
    Bitboard occ = pos.occupied();

    if (us == WHITE) {
        if ((rights & WHITE_OO) && !(occ & (square_bb(SQ_F1) | square_bb(SQ_G1))))
            list.add(make_move(SQ_E1, SQ_G1, CASTLING));
        if ((rights & WHITE_OOO) &&
            !(occ & (square_bb(SQ_D1) | square_bb(SQ_C1) | square_bb(SQ_B1))))
            list.add(make_move(SQ_E1, SQ_C1, CASTLING));
    } else {
        if ((rights & BLACK_OO) && !(occ & (square_bb(SQ_F8) | square_bb(SQ_G8))))
            list.add(make_move(SQ_E8, SQ_G8, CASTLING));
        if ((rights & BLACK_OOO) &&
            !(occ & (square_bb(SQ_D8) | square_bb(SQ_C8) | square_bb(SQ_B8))))
            list.add(make_move(SQ_E8, SQ_C8, CASTLING));
    }
}

} // namespace

void generate_pseudo(const Position& pos, MoveList& list) {
    generate_pawn_moves(pos, list);
    generate_piece_moves(pos, KNIGHT, list);
    generate_piece_moves(pos, BISHOP, list);
    generate_piece_moves(pos, ROOK, list);
    generate_piece_moves(pos, QUEEN, list);
    generate_piece_moves(pos, KING, list);
    generate_castling_moves(pos, list);
}

void generate_legal(Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_pseudo(pos, pseudo);

    Color us = pos.side_to_move();
    Color them = Color(us ^ 1);

    for (Move m : pseudo) {
        if (flag_of(m) == CASTLING) {
            // The king may not start, pass through, or land on an attacked square.
            Square from = from_sq(m);
            Square to = to_sq(m);
            int step = (to > from) ? 1 : -1;
            bool safe = true;
            for (Square s = from;; s = Square(int(s) + step)) {
                if (pos.square_attacked_by(s, them)) {
                    safe = false;
                    break;
                }
                if (s == to) break;
            }
            if (!safe) continue;
        }

        // Apply/undo in place instead of deep-copying the whole Position per
        // candidate move, matching the pattern already used in search.cpp/perft.cpp.
        StateInfo st;
        pos.do_move(m, st);
        bool legal = !pos.square_attacked_by(pos.king_square(us), them);
        pos.undo_move(m, st);
        if (legal) list.add(m);
    }
}

} // namespace chess
