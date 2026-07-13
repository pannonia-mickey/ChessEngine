#pragma once

#include "move.hpp"

namespace chess {

class Position;

// Static Exchange Evaluation: net centipawn material result of playing out
// the full capture sequence on to_sq(m), from the perspective of the side
// making move m, assuming both sides always continue with their least
// valuable attacker, but only when doing so doesn't worsen their own
// result.
// Precondition: m is a capture (normal or en passant) and not a promotion.
int see(const Position& pos, Move m);

} // namespace chess
