#include "pawn_eval.hpp"

#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr Bitboard FILE_BB[8] = {
    FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
    FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
};

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

// Flat penalty for a pawn with no same-color pawn on either adjacent
// file, at any rank - it can never be defended by a pawn advance.
constexpr int ISOLATED_MG = -12;
constexpr int ISOLATED_EG = -18;

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

bool is_isolated_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard neighbor_files = 0;
    if (f > 0) neighbor_files |= FILE_BB[f - 1];
    if (f < 7) neighbor_files |= FILE_BB[f + 1];
    return (own_pawns & neighbor_files) == 0;
}

void pawn_structure_for_color(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard scan = pawns;
    while (scan) {
        Square s = pop_lsb(scan);
        if (is_passed_pawn(pos, c, s)) {
            int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            mg += PASSED_PAWN_MG[rel_rank];
            eg += PASSED_PAWN_EG[rel_rank];
        }
        if (is_isolated_pawn(pos, c, s)) {
            mg += ISOLATED_MG;
            eg += ISOLATED_EG;
        }
    }
}

} // namespace

void pawn_structure(const Position& pos, int& mg, int& eg) {
    int mg_w, eg_w, mg_b, eg_b;
    pawn_structure_for_color(pos, WHITE, mg_w, eg_w);
    pawn_structure_for_color(pos, BLACK, mg_b, eg_b);
    mg = mg_w - mg_b;
    eg = eg_w - eg_b;
}

} // namespace chess
