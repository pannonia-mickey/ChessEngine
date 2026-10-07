#include "see.hpp"

#include "bitboard.hpp"
#include "magic.hpp"

namespace chess {

bool see_ge(const Position& pos, Move move, int threshold) {
    if (move.type() == MoveType::Castling) {
        return threshold <= 0;
    }
    const Square from = move.from();
    const Square to = move.to();
    Bitboard occupied = pos.pieces() ^ square_bb(from);

    // What the first move wins, and the piece then standing on `to`.
    int gain = see_value(type_of(pos.piece_on(to)));
    PieceType on_target = type_of(pos.piece_on(from));
    if (move.type() == MoveType::EnPassant) {
        gain = see_value(Pawn);
        occupied ^= square_bb(make_square(file_of(to), rank_of(from)));
    } else if (move.type() == MoveType::Promotion) {
        gain += see_value(move.promotion()) - see_value(Pawn);
        on_target = move.promotion();
    }
    occupied |= square_bb(to);

    // `swap` is what the side to capture next must win back for the exchange to go its way.
    // If even keeping the first win falls short of the threshold, the move fails; if losing the
    // piece on the target square right away still meets it, the move succeeds whatever follows.
    int swap = gain - threshold;
    if (swap < 0) {
        return false;
    }
    swap = see_value(on_target) - swap;
    if (swap <= 0) {
        return true;
    }

    const Bitboard diagonal_sliders = pos.pieces(Bishop) | pos.pieces(Queen);
    const Bitboard straight_sliders = pos.pieces(Rook) | pos.pieces(Queen);
    Bitboard attackers = pos.attackers_to(to, occupied);
    Color side = pos.side_to_move();
    // Whether the side that moved comes out at or above the threshold if the exchange stops now.
    bool mover_wins = true;
    while (true) {
        side = ~side;
        attackers &= occupied;
        const Bitboard own_attackers = attackers & pos.pieces(side);
        if (own_attackers == 0) {
            break;
        }
        // The side to capture recaptures with its least valuable attacker.
        auto attacker = Pawn;
        while ((own_attackers & pos.pieces(attacker)) == 0) {
            attacker = static_cast<PieceType>(attacker + 1);
        }
        mover_wins = !mover_wins;
        if (attacker == King) {
            // The king may only capture when the opponent has no attacker left.
            return (attackers & pos.pieces(~side)) != 0 ? !mover_wins : mover_wins;
        }
        // Stop when even losing the capturing piece in turn no longer swings the result back.
        swap = see_value(attacker) - swap;
        if (swap < (mover_wins ? 1 : 0)) {
            break;
        }
        occupied ^= square_bb(lsb(own_attackers & pos.pieces(attacker)));
        // Removing the capturer may uncover a slider behind it.
        if (attacker == Pawn || attacker == Bishop || attacker == Queen) {
            attackers |= bishop_attacks(to, occupied) & diagonal_sliders;
        }
        if (attacker == Rook || attacker == Queen) {
            attackers |= rook_attacks(to, occupied) & straight_sliders;
        }
    }
    return mover_wins;
}

}  // namespace chess
