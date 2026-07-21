#pragma once

namespace chess {

class Position;

// Isolated + doubled + backward penalties and the passed-pawn bonus,
// tapered MG/EG, White minus Black. Backed by a process-lifetime pawn
// hash cache keyed on pos.pawn_key() - never needs clearing, since a
// given pawn structure scores identically in any position/game.
void pawn_structure(const Position& pos, int& mg, int& eg);

// Same computation as pawn_structure(), but always recomputed from
// scratch, bypassing the pawn hash cache. Exposed only so tests can
// cross-check the cached and uncached paths against each other (see
// tests/test_eval.cpp's pawn hash consistency test) - not meant to be
// called from eval.cpp.
void pawn_structure_uncached(const Position& pos, int& mg, int& eg);

} // namespace chess
