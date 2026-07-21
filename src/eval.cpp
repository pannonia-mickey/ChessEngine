#include "eval.hpp"
#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"
#include "eval_tables.hpp"
#include "pawn_eval.hpp"

#include <algorithm>

namespace chess {

namespace {

int taper(int mg, int eg, int phase) {
    return (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX;
}

// Mobility bonus: attacked squares not occupied by the piece's own side,
// weighted per piece type (knights/bishops get more weight per square
// since they have fewer reachable squares in absolute terms than rooks/
// queens). Pawns and kings are excluded - not a meaningful signal here.
int mobility(const Position& pos, Color c) {
    static constexpr int WEIGHT[PIECE_TYPE_NB] = {0, 4, 3, 2, 1, 0};
    Bitboard occ = pos.occupied();
    Bitboard own = pos.pieces(c);
    int score = 0;

    Bitboard knights = pos.pieces(c, KNIGHT);
    while (knights) {
        Square s = pop_lsb(knights);
        score += WEIGHT[KNIGHT] * popcount(knight_attacks(s) & ~own);
    }
    Bitboard bishops = pos.pieces(c, BISHOP);
    while (bishops) {
        Square s = pop_lsb(bishops);
        score += WEIGHT[BISHOP] * popcount(bishop_attacks(s, occ) & ~own);
    }
    Bitboard rooks = pos.pieces(c, ROOK);
    while (rooks) {
        Square s = pop_lsb(rooks);
        score += WEIGHT[ROOK] * popcount(rook_attacks(s, occ) & ~own);
    }
    Bitboard queens = pos.pieces(c, QUEEN);
    while (queens) {
        Square s = pop_lsb(queens);
        score += WEIGHT[QUEEN] * popcount(queen_attacks(s, occ) & ~own);
    }
    return score;
}

// Flat bonus for holding both bishops (better long-term minor-piece
// coordination and color-complex control than a lone bishop).
constexpr int BISHOP_PAIR_BONUS = 30;

int bishop_pair(const Position& pos, Color c) {
    return popcount(pos.pieces(c, BISHOP)) >= 2 ? BISHOP_PAIR_BONUS : 0;
}

// Bonus for a rook with no pawns of its own on its file (open) or only
// enemy pawns on its file (semi-open) - a well-known heuristic reflecting
// the file's importance for rook activity/pressure.
constexpr int ROOK_OPEN_FILE_BONUS = 20;
constexpr int ROOK_SEMI_OPEN_FILE_BONUS = 10;

int rook_file_bonus(const Position& pos, Color c) {
    static constexpr Bitboard FILE_BB[8] = {
        FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
        FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
    };
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    int score = 0;

    Bitboard rooks = pos.pieces(c, ROOK);
    while (rooks) {
        Square s = pop_lsb(rooks);
        Bitboard file_bb = FILE_BB[file_of(s)];
        bool has_own = file_bb & own_pawns;
        bool has_enemy = file_bb & enemy_pawns;
        if (!has_own && !has_enemy) score += ROOK_OPEN_FILE_BONUS;
        else if (!has_own && has_enemy) score += ROOK_SEMI_OPEN_FILE_BONUS;
    }
    return score;
}

// Bonus per own pawn found on the two ranks directly in front of the king,
// across the king's file and the two adjacent files (clamped at the board
// edge). MG-only: king safety stops being relevant once enough material
// has been traded off that there's nothing left to attack the king with.
constexpr int KING_SHIELD_BONUS = 10;

int king_safety(const Position& pos, Color c) {
    Square ks = pos.king_square(c);
    int kf = file_of(ks), kr = rank_of(ks);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    int dir = (c == WHITE) ? 1 : -1;
    int shield = 0;

    for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); ++f) {
        for (int dr = 1; dr <= 2; ++dr) {
            int r = kr + dir * dr;
            if (r < 0 || r > 7) continue;
            if (own_pawns & square_bb(make_square(f, r))) shield += KING_SHIELD_BONUS;
        }
    }
    return shield;
}

}  // namespace

int evaluate(const Position& pos) {
    // Material + PST (White minus Black, pre-taper) and game phase are kept
    // incrementally up to date by Position::put_piece()/remove_piece() -
    // see position.hpp - instead of rescanning every piece here on every
    // call, which used to dominate quiescence-leaf and RFP-node cost.
    int mg_score = pos.mg_psq();
    int eg_score = pos.eg_psq();

    int pawn_mg, pawn_eg;
    pawn_structure(pos, pawn_mg, pawn_eg);
    mg_score += pawn_mg;
    eg_score += pawn_eg;
    mg_score += king_safety(pos, WHITE) - king_safety(pos, BLACK);

    int phase = pos.phase();
    int flat_score = mobility(pos, WHITE) - mobility(pos, BLACK)
                    + bishop_pair(pos, WHITE) - bishop_pair(pos, BLACK)
                    + rook_file_bonus(pos, WHITE) - rook_file_bonus(pos, BLACK);
    int score = taper(mg_score, eg_score, phase) + flat_score;

    // Return score from side-to-move's perspective
    if (pos.side_to_move() == BLACK) {
        score = -score;
    }

    return score;
}

}  // namespace chess
