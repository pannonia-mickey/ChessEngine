#ifndef CHESS_EVAL_HPP
#define CHESS_EVAL_HPP

namespace chess {

class Position;

// Evaluate a position from the side-to-move's perspective.
// Positive score = better for the side to move.
// Centipawns (100 = 1 pawn).
int evaluate(const Position& pos);

}

#endif // CHESS_EVAL_HPP
