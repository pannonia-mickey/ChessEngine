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
constexpr std::uint64_t SEED = 0x9E3779B97F4A7C15ULL;
std::uint64_t g_state = SEED;

std::uint64_t next() {
    g_state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = g_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace

void init() {
    // Reset the PRNG to its fixed seed on every call: init() is called
    // from many independent call sites (production startup, and the top
    // of most test cases), and without this reset each call would resume
    // consuming g_state where the previous call left off, silently
    // producing a different (non-reproducible) key set depending on how
    // many times init() had already run in this process - contradicting
    // the "fixed seed... reproducible between runs" comment above. That
    // drift was observed to change TT hash-slot aliasing enough to shift
    // search node counts by 6 figures between otherwise identical
    // searches, depending purely on unrelated tests' run order.
    g_state = SEED;
    for (int p = 0; p < PIECE_NB; ++p)
        for (int s = 0; s < SQUARE_NB; ++s)
            psq[p][s] = next();
    for (int i = 0; i < 16; ++i) castling[i] = next();
    for (int f = 0; f < 8; ++f) ep_file[f] = next();
    side = next();
}

} // namespace chess::zobrist
