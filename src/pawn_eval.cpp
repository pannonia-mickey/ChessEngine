#include "pawn_eval.hpp"

#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"

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

// Penalty applied once per extra pawn beyond the first on a file (a
// 3-pawn stack applies this twice, not once and not three times).
constexpr int DOUBLED_MG = -8;
constexpr int DOUBLED_EG = -16;

// Penalty for a pawn that (a) isn't passed, (b) has no same-color pawn
// on an adjacent file positioned to ever support it (i.e. at the same
// or a less advanced rank), and (c) can't safely advance because an
// enemy pawn controls the square directly ahead.
constexpr int BACKWARD_MG = -8;
constexpr int BACKWARD_EG = -12;

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

// A pawn of color `by` attacks square `s`? Reverse trick matching
// Position::square_attacked_by()'s pattern (src/position.cpp), but
// restricted to pawns only - the backward-pawn definition below
// specifically means "an enemy pawn controls the stop square", not
// "any enemy piece".
bool pawn_attacks_square(const Position& pos, Square s, Color by) {
    Color opponent = Color(by ^ 1);
    return (pawn_attacks(opponent, s) & pos.pieces(by, PAWN)) != 0;
}

// Precondition: `s` is not a passed pawn (the caller checks this first,
// so this doesn't redundantly recompute is_passed_pawn()).
bool is_backward_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    int rel_r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);

    for (int nf = f - 1; nf <= f + 1; nf += 2) {
        if (nf < 0 || nf > 7) continue;
        Bitboard neighbors = own_pawns & FILE_BB[nf];
        while (neighbors) {
            Square ns = pop_lsb(neighbors);
            int neighbor_rel_r = (c == WHITE) ? rank_of(ns) : 7 - rank_of(ns);
            if (neighbor_rel_r <= rel_r) return false;
        }
    }

    Color them = Color(c ^ 1);
    Square stop_sq = make_square(f, rank_of(s) + (c == WHITE ? 1 : -1));
    return pawn_attacks_square(pos, stop_sq, them);
}

void pawn_structure_for_color(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard scan = pawns;
    while (scan) {
        Square s = pop_lsb(scan);
        bool passed = is_passed_pawn(pos, c, s);
        if (passed) {
            int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            mg += PASSED_PAWN_MG[rel_rank];
            eg += PASSED_PAWN_EG[rel_rank];
        } else if (is_backward_pawn(pos, c, s)) {
            mg += BACKWARD_MG;
            eg += BACKWARD_EG;
        }
        if (is_isolated_pawn(pos, c, s)) {
            mg += ISOLATED_MG;
            eg += ISOLATED_EG;
        }
    }
    for (int f = 0; f < 8; ++f) {
        int count = popcount(pawns & FILE_BB[f]);
        if (count > 1) {
            mg += DOUBLED_MG * (count - 1);
            eg += DOUBLED_EG * (count - 1);
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
