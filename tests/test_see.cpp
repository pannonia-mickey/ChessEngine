#include "doctest.h"
#include "see.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "move.hpp"
#include "movegen.hpp"
using namespace chess;

namespace {

// Reference (slow but obviously-correct) SEE: recomputes the full attacker
// set from scratch at every exchange level, instead of maintaining it
// incrementally with x-ray updates the way src/see.cpp now does. Kept here
// purely as a test oracle - any divergence from see() below is a bug in the
// incremental version, not a SEE-algorithm bug.
Bitboard slow_attackers_to(const Position& pos, Square sq, Bitboard occ) {
    Bitboard att = 0;
    att |= pawn_attacks(BLACK, sq) & pos.pieces(WHITE, PAWN);
    att |= pawn_attacks(WHITE, sq) & pos.pieces(BLACK, PAWN);
    att |= knight_attacks(sq) & pos.pieces(KNIGHT);
    att |= king_attacks(sq) & pos.pieces(KING);
    att |= bishop_attacks(sq, occ) & (pos.pieces(BISHOP) | pos.pieces(QUEEN));
    att |= rook_attacks(sq, occ) & (pos.pieces(ROOK) | pos.pieces(QUEEN));
    return att & occ;
}

int slow_exchange(const Position& pos, Bitboard occ, Color side, Square sq, int captured_value) {
    Bitboard side_attackers = slow_attackers_to(pos, sq, occ) & pos.pieces(side);
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
    int gain = captured_value -
               slow_exchange(pos, next_occ, Color(side ^ 1), sq, PIECE_VALUE[attacker_type]);
    return std::max(0, gain);
}

int slow_see(const Position& pos, Move m) {
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
    return captured_value - slow_exchange(pos, occ, side, to, PIECE_VALUE[attacker_type]);
}

// see()'s precondition (see.hpp) is a non-promotion capture - production
// code (search.cpp's move ordering) only ever calls it under that
// condition, so the parity walk below matches that instead of exercising
// see() outside its documented contract.
bool is_plain_capture(const Position& pos, Move m) {
    if (flag_of(m) == PROMOTION) return false;
    return flag_of(m) == EN_PASSANT || pos.piece_on(to_sq(m)) != NO_PIECE;
}

// Walks a several-ply tree of legal moves from `pos`, SEE-checking every
// non-promotion capture found at every node against the reference.
void walk_and_check_see(Position& pos, int depth) {
    MoveList moves;
    generate_legal(pos, moves);
    for (Move m : moves) {
        if (!is_plain_capture(pos, m)) continue;
        CHECK(see(pos, m) == slow_see(pos, m));
    }
    if (depth == 0) return;
    StateInfo st;
    for (Move m : moves) {
        pos.do_move(m, st);
        walk_and_check_see(pos, depth - 1);
        pos.undo_move(m, st);
    }
}

} // namespace

TEST_CASE("incremental x-ray SEE matches a from-scratch reference") {
    attacks::init();
    struct { const char* fen; int depth; } cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3},
    };
    for (auto& c : cases) {
        Position p;
        p.set(c.fen);
        walk_and_check_see(p, c.depth);
    }
}

TEST_CASE("SEE: undefended capture returns the captured piece's value") {
    attacks::init();
    // White queen d1, Black pawn d7, nothing else attacks d7.
    Position p; CHECK(p.set("7k/3p4/8/8/8/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D1, SQ_D7)) == 100);
}

TEST_CASE("SEE: losing capture returns a negative value when the defender wins the exchange") {
    attacks::init();
    // White queen d1 captures the pawn on d7, but Black's knight on b8
    // (the only defender) recaptures the queen for free afterwards:
    // net = pawn(100) - queen(900) = -800.
    Position p; CHECK(p.set("1n5k/3p4/8/8/8/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D1, SQ_D7)) == -800);
}

TEST_CASE("SEE: even rook trade nets zero") {
    attacks::init();
    // White rook a1 takes the rook on a8; Black's rook on h8 recaptures
    // along rank 8. Rook for rook: net = 0.
    Position p; CHECK(p.set("r6r/7k/8/8/8/8/8/R6K w - - 0 1"));
    CHECK(see(p, make_move(SQ_A1, SQ_A8)) == 0);
}

TEST_CASE("SEE: declines a further capture that would lose material (stops the exchange early)") {
    attacks::init();
    // White pawn d4 takes the pawn on e5. Black recaptures with the
    // knight on c6 (regaining the pawn: net so far 0). White *could*
    // recapture the knight with the queen on e1, but Black's rook on e8
    // would then win the queen for a rook - a bad trade for White, who
    // therefore should decline it and stop. Final result: 0, not the
    // -580 a naive "always recapture" implementation would compute.
    Position p; CHECK(p.set("4r2k/8/2n5/4p3/3P4/8/8/K3Q3 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D4, SQ_E5)) == 0);
}

TEST_CASE("SEE: reveals an x-ray attacker behind a captured piece") {
    attacks::init();
    // White rook d3 takes the knight on d5 (+320). Black's pawn on c6
    // recaptures the rook (+500 for Black). White's second rook on d1,
    // previously blocked by the d3 rook, is now unblocked and recaptures
    // the pawn (+100 for White). Net for White: 320 - 500 + 100 = -80.
    // An implementation that fails to re-reveal the d1 rook after d3 is
    // removed would instead stop after Black's recapture and return
    // -180, so this distinguishes a working x-ray from a broken one.
    Position p; CHECK(p.set("7k/8/2p5/3n4/8/3R4/8/K2R4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D3, SQ_D5)) == -80);
}

TEST_CASE("SEE: en passant capture uses the actual captured pawn's square") {
    attacks::init();
    // White pawn e5 captures en passant onto d6, removing Black's pawn
    // actually sitting on d5 (not on d6). Black's rook on d1 can only
    // attack d6 - and thus recapture - once d5 is genuinely cleared from
    // the exchange's occupancy: with d5 vacated, the rook's file is open
    // all the way to d6 (500-value rook recaptures the 100-value pawn,
    // netting 0 overall: 100 - 100 = 0). An implementation that mistakenly
    // cleared d6 (the move's destination) instead of d5 (the actual
    // captured square) would leave d5 "occupied" in the simulated
    // occupancy, blocking the rook's file, and would wrongly compute 100
    // (as if the rook were never there) instead of the correct 0.
    Position p; CHECK(p.set("7k/8/8/3pP3/8/8/K7/3r4 w - d6 0 1"));
    CHECK(see(p, make_move(SQ_E5, SQ_D6, EN_PASSANT)) == 0);
}

TEST_CASE("SEE: recaptures with the least valuable attacker, not just any attacker") {
    attacks::init();
    // White pawn e4 takes the knight on d5 (+320). Black has two possible
    // recapturing pieces on d5: the pawn on c6 (100) and the queen on d8
    // (900, via the d-file) - a correct implementation always continues
    // with the *cheaper* one available (the pawn), not just any attacker.
    // After Black recaptures with the pawn, White's rook on d1 could take
    // that pawn (+100), but Black's queen would then win the rook for
    // free (+500 for Black) with nothing further to punish it - a bad
    // continuation White correctly declines. Net: White wins the knight
    // and loses only the initiating pawn: 320 - 100 = 220.
    Position p; CHECK(p.set("3q3k/8/2p5/3n4/4P3/8/K7/3R4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_E4, SQ_D5)) == 220);
}

TEST_CASE("SEE: a winning multi-round exchange nets the full accumulated gain") {
    attacks::init();
    // White knight f4 takes the bishop on d5 (+330). Black's knight on b6
    // recaptures (Black regains 320, matching White's knight now sitting
    // on d5). White's queen on d1 then captures that knight (+320 more)
    // with no further Black attacker left to punish it, so continuing is
    // pure profit and the algorithm correctly does not decline. Net:
    // 330 (bishop) - 320 (White's knight, recaptured) + 320 (Black's
    // knight, captured by the queen) = 330 - a genuine multi-ply gain,
    // not just a single free capture.
    Position p; CHECK(p.set("7k/8/1n6/3b4/5N2/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_F4, SQ_D5)) == 330);
}
