#pragma once

namespace chess {

class Position;

// Isolated pawn penalty and the passed-pawn bonus, tapered MG/EG, White
// minus Black. (Doubled and backward penalties are added by later
// tasks; a pawn hash cache is added by a later task too - this first
// version always computes from scratch.)
void pawn_structure(const Position& pos, int& mg, int& eg);

} // namespace chess
