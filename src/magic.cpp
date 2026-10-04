#include "magic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace chess {
namespace {

using Direction = std::array<int, 2>;  // (file step, rank step)

constexpr std::array<Direction, 4> kRookDirections{{{0, 1}, {0, -1}, {1, 0}, {-1, 0}}};
constexpr std::array<Direction, 4> kBishopDirections{{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

// Largest number of relevant blocker squares for any slider (a rook in a corner).
constexpr std::size_t kMaxSubsets = std::size_t{1} << 12;

// Per-rank seeds that make the search below converge after few candidates (the values Stockfish
// uses with the same generator). The magics themselves are still found at startup.
constexpr std::array<std::uint64_t, 8> kSeeds{728, 10316, 55013, 32803, 12281, 15100, 16645, 255};

// xorshift64* generator. A fixed seed makes the magic search reproducible.
class Prng {
public:
    explicit constexpr Prng(std::uint64_t seed) noexcept : state_(seed) {}

    constexpr std::uint64_t next() noexcept {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

    // Numbers with few set bits make good magic candidates.
    constexpr std::uint64_t sparse() noexcept { return next() & next() & next(); }

private:
    std::uint64_t state_;
};

// Blocker squares that can change the attack set: the empty-board attacks minus the board edges
// (a piece on the edge never hides anything behind it).
Bitboard relevant_mask(PieceType type, Square square) noexcept {
    const Bitboard edges = ((kRank1 | kRank8) & ~rank_bb(rank_of(square))) |
                           ((kFileA | kFileH) & ~file_bb(file_of(square)));
    return sliding_attacks(type, square, 0) & ~edges;
}

template <std::size_t TableSize>
void init_magics(PieceType type, std::array<Magic, kSquareCount>& magics,
                 std::array<Bitboard, TableSize>& table) {
    std::array<Bitboard, kMaxSubsets> occupancies{};
    std::array<Bitboard, kMaxSubsets> references{};
    // epochs[i] == attempt marks table slot i as written during the current attempt, which
    // avoids clearing the slot range before every candidate.
    std::array<int, kMaxSubsets> epochs{};
    int attempt = 0;
    std::uint32_t offset = 0;

    for (int sq = 0; sq < kSquareCount; ++sq) {
        const auto square = static_cast<Square>(sq);
        Magic& magic = magics[square];
        magic.mask = relevant_mask(type, square);
        magic.shift = static_cast<std::uint8_t>(64 - popcount(magic.mask));
        magic.offset = offset;

        // Enumerate every subset of the mask with the Carry-Rippler trick.
        std::size_t size = 0;
        Bitboard subset = 0;
        while (true) {
            occupancies[size] = subset;
            references[size] = sliding_attacks(type, square, subset);
            ++size;
            subset = (subset - magic.mask) & magic.mask;
            if (subset == 0) {
                break;
            }
        }

        Prng prng(kSeeds[static_cast<std::size_t>(rank_of(square))]);
        bool found = false;
        while (!found) {
            magic.magic = prng.sparse();
            if (popcount((magic.mask * magic.magic) >> 56) < 6) {
                continue;  // Too few high bits: cannot spread the subsets well.
            }
            ++attempt;
            found = true;
            for (std::size_t i = 0; i < size; ++i) {
                const std::size_t slot = magic.index(occupancies[i]) - offset;
                if (epochs[slot] < attempt) {
                    epochs[slot] = attempt;
                    table[offset + slot] = references[i];
                } else if (table[offset + slot] != references[i]) {
                    found = false;  // Destructive collision.
                    break;
                }
            }
        }
        offset += static_cast<std::uint32_t>(size);
    }
}

}  // namespace

Bitboard sliding_attacks(PieceType type, Square square, Bitboard occupancy) noexcept {
    const auto& directions = type == Rook ? kRookDirections : kBishopDirections;
    Bitboard attacks = 0;
    for (const auto& [file_step, rank_step] : directions) {
        int file = file_of(square) + file_step;
        int rank = rank_of(square) + rank_step;
        while (file >= 0 && file < 8 && rank >= 0 && rank < 8) {
            const Square target = make_square(file, rank);
            attacks |= square_bb(target);
            if (has(occupancy, target)) {
                break;
            }
            file += file_step;
            rank += rank_step;
        }
    }
    return attacks;
}

SliderTables::SliderTables() {
    init_magics(Rook, rook_magics_, rook_table_);
    init_magics(Bishop, bishop_magics_, bishop_table_);

    for (int from_index = 0; from_index < kSquareCount; ++from_index) {
        const auto from = static_cast<Square>(from_index);
        for (int to_index = 0; to_index < kSquareCount; ++to_index) {
            const auto to = static_cast<Square>(to_index);
            for (const PieceType type : {Bishop, Rook}) {
                const auto attacks = [&](Square square, Bitboard occupancy) {
                    return type == Rook ? rook(square, occupancy) : bishop(square, occupancy);
                };
                if (from != to && has(attacks(from, 0), to)) {
                    line_[from][to] =
                        (attacks(from, 0) & attacks(to, 0)) | square_bb(from) | square_bb(to);
                    between_[from][to] =
                        attacks(from, square_bb(to)) & attacks(to, square_bb(from));
                }
            }
        }
    }
}

Bitboard piece_attacks(PieceType type, Square square, Bitboard occupancy) {
    switch (type) {
        case Knight:
            return knight_attacks(square);
        case Bishop:
            return bishop_attacks(square, occupancy);
        case Rook:
            return rook_attacks(square, occupancy);
        case Queen:
            return queen_attacks(square, occupancy);
        case King:
            return king_attacks(square);
        case Pawn:
        case NoPieceType:
            break;
    }
    return 0;
}

}  // namespace chess
