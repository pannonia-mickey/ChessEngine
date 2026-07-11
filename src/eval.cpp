#include "eval.hpp"
#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"

namespace chess {

// Material values in centipawns
constexpr int MATERIAL_VALUE[PIECE_TYPE_NB] = {
    100,  // PAWN
    320,  // KNIGHT
    330,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    0     // KING (no material value)
};

// Piece-Square Tables (PST) for all piece types
// Indexed by square (LERF: a1=0, h8=63)
// Values in centipawns
// Standard mid-game tables based on common chess engine heuristics

constexpr int PST[PIECE_TYPE_NB][SQUARE_NB] = {
    // PAWN
    {
         0,   0,   0,   0,   0,   0,   0,   0,
        50,  50,  50,  50,  50,  50,  50,  50,
        10,  10,  20,  30,  30,  20,  10,  10,
         5,   5,  10,  25,  25,  10,   5,   5,
         0,   0,   5,  20,  20,   5,   0,   0,
         5,  -5, -10,   0,   0, -10,  -5,   5,
         5,  10,  10, -20, -20,  10,  10,   5,
         0,   0,   0,   0,   0,   0,   0,   0
    },
    // KNIGHT
    {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50
    },
    // BISHOP
    {
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   5,   5,  10,  10,   5,   5, -10,
        -10,   0,  10,  10,  10,  10,   0, -10,
        -10,  10,  10,  10,  10,  10,  10, -10,
        -10,   5,   0,   0,   0,   0,   5, -10,
        -20, -10, -10, -10, -10, -10, -10, -20
    },
    // ROOK
    {
         0,   0,   0,   0,   0,   0,   0,   0,
         5,  10,  10,  10,  10,  10,  10,   5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
         0,   0,   0,   5,   5,   0,   0,   0
    },
    // QUEEN
    {
        -20, -10, -10,  -5,  -5, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
         -5,   0,   5,   5,   5,   5,   0,  -5,
          0,   0,   5,   5,   5,   5,   0,  -5,
        -10,   5,   5,   5,   5,   5,   0, -10,
        -10,   0,   5,   0,   0,   0,   0, -10,
        -20, -10, -10,  -5,  -5, -10, -10, -20
    },
    // KING
    {
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
         20,  20,   0,   0,   0,   0,  20,  20,
         20,  30,  10,   0,   0,  10,  30,  20
    }
};

int evaluate(const Position& pos) {
    int score = 0;

    // Iterate through each piece type and color
    for (int pt_idx = PAWN; pt_idx < PIECE_TYPE_NB; ++pt_idx) {
        PieceType pt = static_cast<PieceType>(pt_idx);

        // White pieces
        Bitboard white_pieces = pos.pieces(WHITE, pt);
        while (white_pieces) {
            Square s = pop_lsb(white_pieces);
            // PST rows are written rank 8 first (row 0 = rank 8). LERF index 0
            // is a1, so flip White's square to land it on the bottom row.
            Square mirrored = static_cast<Square>(s ^ 56);
            score += MATERIAL_VALUE[pt] + PST[pt][mirrored];
        }

        // Black pieces
        Bitboard black_pieces = pos.pieces(BLACK, pt);
        while (black_pieces) {
            Square s = pop_lsb(black_pieces);
            // Black's back rank is rank 8 (row 0), so it reads the table directly.
            score -= MATERIAL_VALUE[pt] + PST[pt][s];
        }
    }

    // Return score from side-to-move's perspective
    if (pos.side_to_move() == BLACK) {
        score = -score;
    }

    return score;
}

}  // namespace chess
