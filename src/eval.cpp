#include "eval.hpp"
#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"
#include "eval_tables.hpp"

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

// Detect whether a pawn is passed: no enemy pawns on the pawn's file or
// adjacent files ahead of it (in the direction of advancement).
bool is_passed_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s), r = rank_of(s);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    for (int nf = std::max(0, f - 1); nf <= std::min(7, f + 1); ++nf) {
        for (int nr = 0; nr < 8; ++nr) {
            bool ahead = (c == WHITE) ? (nr > r) : (nr < r);
            if (!ahead) continue;
            if (enemy_pawns & square_bb(make_square(nf, nr))) return false;
        }
    }
    return true;
}

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

void passed_pawn_bonus(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    while (pawns) {
        Square s = pop_lsb(pawns);
        if (!is_passed_pawn(pos, c, s)) continue;
        int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
        mg += PASSED_PAWN_MG[rel_rank];
        eg += PASSED_PAWN_EG[rel_rank];
    }
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

    int pp_mg_w, pp_eg_w, pp_mg_b, pp_eg_b;
    passed_pawn_bonus(pos, WHITE, pp_mg_w, pp_eg_w);
    passed_pawn_bonus(pos, BLACK, pp_mg_b, pp_eg_b);
    mg_score += pp_mg_w - pp_mg_b;
    eg_score += pp_eg_w - pp_eg_b;
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
