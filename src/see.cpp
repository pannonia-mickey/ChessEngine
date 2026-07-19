#include "see.hpp"

#include <algorithm>

#include "attacks.hpp"
#include "bitboard.hpp"
#include "position.hpp"
#include "types.hpp"

namespace chess {

namespace {

// Every piece of either color that attacks `sq`, given custom occupancy
// `occ` (not necessarily pos.occupied() - the caller may have removed
// pieces to simulate a capture sequence). Sliding-piece attacks are
// computed against `occ`, so this is only ever called once per see() (to
// seed the exchange), not per recursion level - see exchange()'s comment
// for why a full rescan isn't needed at every step.
Bitboard attackers_to(const Position& pos, Square sq, Bitboard occ) {
    Bitboard att = 0;
    att |= pawn_attacks(BLACK, sq) & pos.pieces(WHITE, PAWN);
    att |= pawn_attacks(WHITE, sq) & pos.pieces(BLACK, PAWN);
    att |= knight_attacks(sq) & pos.pieces(KNIGHT);
    att |= king_attacks(sq) & pos.pieces(KING);
    Bitboard diagonal_sliders = pos.pieces(BISHOP) | pos.pieces(QUEEN);
    att |= bishop_attacks(sq, occ) & diagonal_sliders;
    Bitboard straight_sliders = pos.pieces(ROOK) | pos.pieces(QUEEN);
    att |= rook_attacks(sq, occ) & straight_sliders;
    return att & occ;
}

bool aligned_rank_or_file(Square a, Square b) {
    return file_of(a) == file_of(b) || rank_of(a) == rank_of(b);
}

bool aligned_diagonal(Square a, Square b) {
    return (file_of(a) - rank_of(a)) == (file_of(b) - rank_of(b)) ||
           (file_of(a) + rank_of(a)) == (file_of(b) + rank_of(b));
}

// Resolves the rest of the exchange on `sq` for `side`, given that the
// piece now sitting on `sq` is worth `captured_value` to whoever captures
// it next. Returns the best net gain `side` can achieve by continuing to
// capture, clamped to 0 - `side` can always choose to stop instead, which
// is what lets the recursion correctly decline a losing continuation
// (e.g. not recapturing with a queen if it would just be lost to a
// further defender for nothing).
//
// `attackers` is the both-colors attacker set to `sq` against the current
// scratch occupancy `occ`, maintained incrementally instead of recomputed
// from scratch at every level (attackers_to() above did 5 lookups - 2
// pawn-direction, knight, king, and both sliders - every single recursion
// step, dominating SEE's cost in tactical/quiescence-heavy positions).
// Removing one piece from `occ` can only ever *add* new attackers (an
// x-ray slider revealed behind the piece just removed), never remove any
// beyond the one explicitly cleared, so it's always sound to start from
// the previous set, drop the consumed square, and OR in a fresh scan of
// just the ray(s) that square could have been blocking - which is only
// non-empty when that square was actually aligned with `sq` in the first
// place (true for at most one of rank/file or diagonal, since a square
// can't be both unless it coincides with `sq` itself).
int exchange(const Position& pos, Bitboard occ, Bitboard attackers, Bitboard diagonal_sliders,
             Bitboard straight_sliders, Color side, Square sq, int captured_value) {
    Bitboard side_attackers = attackers & pos.pieces(side);
    if (!side_attackers) return 0;

    PieceType attacker_type = NO_PIECE_TYPE;
    Square attacker_sq = SQ_NONE;
    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard b = side_attackers & pos.pieces(side, static_cast<PieceType>(pt));
        if (b) {
            attacker_type = static_cast<PieceType>(pt);
            attacker_sq = lsb(b);
            break;
        }
    }

    occ &= ~square_bb(attacker_sq);
    attackers &= ~square_bb(attacker_sq);
    if (aligned_rank_or_file(attacker_sq, sq)) {
        attackers |= rook_attacks(sq, occ) & straight_sliders & occ;
    }
    if (aligned_diagonal(attacker_sq, sq)) {
        attackers |= bishop_attacks(sq, occ) & diagonal_sliders & occ;
    }

    int gain = captured_value - exchange(pos, occ, attackers, diagonal_sliders, straight_sliders,
                                          Color(side ^ 1), sq, PIECE_VALUE[attacker_type]);
    return std::max(0, gain);
}

} // namespace

int see(const Position& pos, Move m) {
    Square from = from_sq(m);
    Square to = to_sq(m);
    MoveFlag mf = flag_of(m);

    Bitboard occ = pos.occupied();
    occ &= ~square_bb(from);

    int captured_value;
    if (mf == EN_PASSANT) {
        captured_value = PIECE_VALUE[PAWN];
        Square captured_sq = make_square(file_of(to), rank_of(from));
        occ &= ~square_bb(captured_sq);
    } else {
        captured_value = PIECE_VALUE[type_of(pos.piece_on(to))];
    }

    PieceType attacker_type = type_of(pos.piece_on(from));
    Color side = Color(pos.side_to_move() ^ 1);

    Bitboard diagonal_sliders = pos.pieces(BISHOP) | pos.pieces(QUEEN);
    Bitboard straight_sliders = pos.pieces(ROOK) | pos.pieces(QUEEN);
    Bitboard attackers = attackers_to(pos, to, occ);

    return captured_value - exchange(pos, occ, attackers, diagonal_sliders, straight_sliders, side,
                                      to, PIECE_VALUE[attacker_type]);
}

} // namespace chess
