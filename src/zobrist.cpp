#include "zobrist.hpp"

namespace chess::zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key castling[16];
Key ep_file[8];
Key side;

namespace {

// splitmix64: a small, fast, fixed-seed PRNG. Fixed seed (not
// std::random_device) so keys - and therefore TT contents - are reproducible
// between runs, which matters for debugging search behavior.
std::uint64_t g_state = 0x9E3779B97F4A7C15ULL;

std::uint64_t next() {
    g_state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = g_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace

void init() {
    for (int p = 0; p < PIECE_NB; ++p)
        for (int s = 0; s < SQUARE_NB; ++s)
            psq[p][s] = next();
    for (int i = 0; i < 16; ++i) castling[i] = next();
    for (int f = 0; f < 8; ++f) ep_file[f] = next();
    side = next();
}

} // namespace chess::zobrist
