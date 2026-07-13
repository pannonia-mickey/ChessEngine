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
// recomputed against `occ` each call, so removing a piece in front of a
// bishop/rook/queen correctly reveals that slider as a new attacker.
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

// Resolves the rest of the exchange on `sq` for `side`, given that the
// piece now sitting on `sq` is worth `captured_value` to whoever captures
// it next. Returns the best net gain `side` can achieve by continuing to
// capture, clamped to 0 - `side` can always choose to stop instead, which
// is what lets the recursion correctly decline a losing continuation
// (e.g. not recapturing with a queen if it would just be lost to a
// further defender for nothing).
int exchange(const Position& pos, Bitboard occ, Color side, Square sq, int captured_value) {
    Bitboard side_attackers = attackers_to(pos, sq, occ) & pos.pieces(side);
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

    Bitboard next_occ = occ & ~square_bb(attacker_sq);
    int gain = captured_value - exchange(pos, next_occ, Color(side ^ 1), sq, PIECE_VALUE[attacker_type]);
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
    return captured_value - exchange(pos, occ, side, to, PIECE_VALUE[attacker_type]);
}

} // namespace chess
