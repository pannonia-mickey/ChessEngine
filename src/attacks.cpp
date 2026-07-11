#include "attacks.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace chess {

// File-scope lookup tables for attack sets
static Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
static Bitboard KnightAttacks[SQUARE_NB];
static Bitboard KingAttacks[SQUARE_NB];

// --- Magic bitboards for sliding pieces (bishop/rook) ---
//
// Per-square magic entry: the relevance mask, the magic multiplier, the
// shift amount, and a pointer into a shared attack-table pool.
namespace {

struct Magic {
    Bitboard mask = 0;
    Bitboard magic = 0;
    unsigned shift = 0;
    Bitboard* attacks = nullptr;
};

Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];
std::vector<Bitboard> RookTable;
std::vector<Bitboard> BishopTable;

// Deltas (file, rank) for the four rays of each slider.
constexpr int RookDeltas[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
constexpr int BishopDeltas[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// True sliding attack set from `sq` given real board occupancy `occ`:
// walk each ray one square at a time, including the first blocker (if any)
// and then stopping.
Bitboard sliding_attack(Square sq, Bitboard occ, const int deltas[4][2]) {
    Bitboard attacks = 0;
    int fl = file_of(sq), rk = rank_of(sq);
    for (int d = 0; d < 4; ++d) {
        int df = deltas[d][0], dr = deltas[d][1];
        int f = fl + df, r = rk + dr;
        while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
            Square s2 = make_square(f, r);
            attacks |= square_bb(s2);
            if (occ & square_bb(s2)) break;
            f += df;
            r += dr;
        }
    }
    return attacks;
}

// Relevance mask: same rays as sliding_attack on an empty board, but the
// final (edge) square of each ray is excluded, since it never blocks
// anything and would only bloat the occupancy-subset count.
Bitboard sliding_mask(Square sq, const int deltas[4][2]) {
    Bitboard mask = 0;
    int fl = file_of(sq), rk = rank_of(sq);
    for (int d = 0; d < 4; ++d) {
        int df = deltas[d][0], dr = deltas[d][1];
        int f = fl + df, r = rk + dr;
        while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
            int nf = f + df, nr = r + dr;
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) break; // (f, r) is the edge square
            mask |= square_bb(make_square(f, r));
            f = nf;
            r = nr;
        }
    }
    return mask;
}

// Simple xorshift64 PRNG, fixed seed for reproducible magic search.
std::uint64_t rng_state = 88172645463325252ULL;

std::uint64_t rand64() {
    std::uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

// Biased towards sparse bit patterns, which tend to make good magic candidates.
Bitboard random_magic_candidate() {
    return rand64() & rand64() & rand64();
}

// Search for a magic multiplier for square `sq` with relevance mask `mask`.
// Fills `attacks_out[0 .. (1 << popcount(mask)) - 1]` with the corresponding
// attack sets (indexed by the magic hash) as a side effect of verifying the
// candidate, and returns the accepted magic number.
Bitboard find_magic(Square sq, Bitboard mask, const int deltas[4][2], Bitboard* attacks_out) {
    int bits = popcount(mask);
    int size = 1 << bits;
    unsigned shift = 64 - bits;

    // Enumerate every occupancy subset of mask (carry-rippler trick) along
    // with its true (reference) attack set.
    std::vector<Bitboard> occupancies(size);
    std::vector<Bitboard> reference(size);
    Bitboard subset = 0;
    int count = 0;
    do {
        occupancies[count] = subset;
        reference[count] = sliding_attack(sq, subset, deltas);
        ++count;
        subset = (subset - mask) & mask;
    } while (subset != 0);

    // `used`/`epoch` avoid re-clearing the whole table on every attempt: a
    // slot is only considered populated for the current attempt if its
    // epoch matches. This turns each attempt's cost into just the (often
    // short-circuited) verification loop instead of an O(size) reset.
    std::vector<Bitboard> used(size);
    std::vector<int> epoch(size, 0);
    int current_epoch = 0;

    // Defensive cap: real magic search converges in well under a few thousand
    // attempts per square, so this should never trigger in a legitimate run.
    // It exists so a future regression in the mask/delta logic fails loudly
    // at startup instead of hanging forever.
    constexpr long long kMaxAttempts = 100000000LL;
    for (long long attempt = 0; attempt < kMaxAttempts; ++attempt) {
        ++current_epoch;
        Bitboard magic = random_magic_candidate();
        bool collision = false;
        for (int i = 0; i < size && !collision; ++i) {
            Bitboard index = (occupancies[i] * magic) >> shift;
            if (epoch[index] != current_epoch) {
                epoch[index] = current_epoch;
                used[index] = reference[i];
            } else if (used[index] != reference[i]) {
                collision = true;
            }
        }
        if (!collision) {
            std::copy(used.begin(), used.end(), attacks_out);
            return magic;
        }
    }

    std::cerr << "[attacks] find_magic: exceeded " << kMaxAttempts
               << " attempts without finding a valid magic number\n";
    std::abort();
}

// Compute masks/sizes for every square, allocate the shared pool, then find
// a magic per square and populate its attack-table slice.
void init_slider(Magic magics[SQUARE_NB], std::vector<Bitboard>& table, const int deltas[4][2]) {
    Bitboard masks[SQUARE_NB];
    int sizes[SQUARE_NB];
    int total = 0;
    for (int i = 0; i < SQUARE_NB; ++i) {
        masks[i] = sliding_mask(Square(i), deltas);
        sizes[i] = 1 << popcount(masks[i]);
        total += sizes[i];
    }

    table.assign(total, 0ULL);

    int offset = 0;
    for (int i = 0; i < SQUARE_NB; ++i) {
        Square s = Square(i);
        Magic& m = magics[i];
        m.mask = masks[i];
        m.shift = 64 - popcount(masks[i]);
        m.attacks = table.data() + offset;
        m.magic = find_magic(s, masks[i], deltas, m.attacks);
        offset += sizes[i];
    }
}

// Shared by bishop_attacks()/rook_attacks() so an indexing fix only has to
// be made in one place.
Bitboard magic_index_lookup(const Magic& m, Bitboard occupied) {
    Bitboard index = ((occupied & m.mask) * m.magic) >> m.shift;
    return m.attacks[index];
}

} // namespace

namespace attacks {

void init() {
    // Initialize pawn attacks
    for (int i = 0; i < SQUARE_NB; ++i) {
        Square s = Square(i);
        Bitboard bb = square_bb(s);

        // White pawn attacks: move one square north, then attack diagonally (east and west)
        Bitboard white_pawn_bb = shift_north(bb);
        PawnAttacks[WHITE][s] = shift_east(white_pawn_bb) | shift_west(white_pawn_bb);

        // Black pawn attacks: move one square south, then attack diagonally (east and west)
        Bitboard black_pawn_bb = shift_south(bb);
        PawnAttacks[BLACK][s] = shift_east(black_pawn_bb) | shift_west(black_pawn_bb);
    }

    // Initialize knight attacks
    // Knights move in an L-shape: 2 squares in one direction, 1 in a perpendicular direction
    for (int i = 0; i < SQUARE_NB; ++i) {
        Square s = Square(i);
        Bitboard bb = square_bb(s);
        Bitboard attacks = 0ULL;

        // All 8 knight moves: compose shift functions
        // +2 north, +1 east
        attacks |= shift_east(shift_north(shift_north(bb)));
        // +2 north, -1 west
        attacks |= shift_west(shift_north(shift_north(bb)));
        // -2 north (south), +1 east
        attacks |= shift_east(shift_south(shift_south(bb)));
        // -2 north (south), -1 west
        attacks |= shift_west(shift_south(shift_south(bb)));

        // +1 north, +2 east
        attacks |= shift_east(shift_east(shift_north(bb)));
        // +1 north, -2 west
        attacks |= shift_west(shift_west(shift_north(bb)));
        // -1 north (south), +2 east
        attacks |= shift_east(shift_east(shift_south(bb)));
        // -1 north (south), -2 west
        attacks |= shift_west(shift_west(shift_south(bb)));

        KnightAttacks[s] = attacks;
    }

    // Initialize king attacks
    for (int i = 0; i < SQUARE_NB; ++i) {
        Square s = Square(i);
        Bitboard bb = square_bb(s);
        Bitboard attacks = 0ULL;

        // King can move one square in all 8 directions
        attacks |= shift_north(bb);
        attacks |= shift_south(bb);
        attacks |= shift_east(bb);
        attacks |= shift_west(bb);
        attacks |= shift_north_east(bb);
        attacks |= shift_north_west(bb);
        attacks |= shift_south_east(bb);
        attacks |= shift_south_west(bb);

        KingAttacks[s] = attacks;
    }

    // Compute magics and populate sliding-piece attack tables at runtime.
    init_slider(RookMagics, RookTable, RookDeltas);
    init_slider(BishopMagics, BishopTable, BishopDeltas);
}

} // namespace attacks

// Query functions - simple array lookups
Bitboard pawn_attacks(Color c, Square s) {
    return PawnAttacks[c][s];
}

Bitboard knight_attacks(Square s) {
    return KnightAttacks[s];
}

Bitboard king_attacks(Square s) {
    return KingAttacks[s];
}

Bitboard bishop_attacks(Square s, Bitboard occupied) {
    return magic_index_lookup(BishopMagics[s], occupied);
}

Bitboard rook_attacks(Square s, Bitboard occupied) {
    return magic_index_lookup(RookMagics[s], occupied);
}

} // namespace chess
