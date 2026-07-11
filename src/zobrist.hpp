#pragma once
#include <cstdint>
#include "types.hpp"

namespace chess::zobrist {

using Key = std::uint64_t;

// Must be called once before any Position is hashed (mirrors attacks::init()).
// Uses a fixed-seed PRNG, so keys are reproducible across runs/platforms.
void init();

extern Key psq[PIECE_NB][SQUARE_NB];
extern Key castling[16];
extern Key ep_file[8];
extern Key side;

} // namespace chess::zobrist
