#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "evaluate.hpp"
#include "move.hpp"
#include "types.hpp"

namespace chess {

// How a stored score relates to the true value of the position.
enum class Bound : std::uint8_t {
    None,
    Upper,  // The search failed low: the true value is at most the score.
    Lower,  // The search failed high: the true value is at least the score.
    Exact,
};

// The stored static evaluation of a position that has none: one in check, which is not evaluated.
// Below every score, and still fits the table's 16 bits.
inline constexpr Score kNoEval = std::numeric_limits<std::int16_t>::min();

struct TtEntry {
    Move move;
    Score score = 0;
    // The static evaluation of the position, or kNoEval.
    Score eval = kNoEval;
    int depth = 0;
    Bound bound = Bound::None;
};

// A hash table of search results keyed by the Zobrist key, so positions reached through
// different move orders (transpositions) and positions seen in earlier iterations are not
// searched from scratch. Each result also carries the position's static evaluation, so a
// position found in the table is not evaluated again.
//
// The table is an array of buckets, one cache line each, holding kBucketSize entries; the low
// bits of the key select the bucket. A store for a position already in the bucket replaces its
// entry unless that holds a deeper, non-exact result (or any deeper result, for a store at
// depth 0) from the current search. A store for a new position replaces an empty entry, or else
// the least valuable one: the shallowest, counting results of earlier searches as shallower the
// older they are. Mate scores are stored as given: the caller makes them relative to the node
// (see search.cpp).
//
// Not thread-safe: one search uses the table at a time.
class TranspositionTable {
public:
    static constexpr std::size_t kDefaultSizeMb = 16;
    static constexpr std::size_t kMinSizeMb = 1;
    static constexpr std::size_t kMaxSizeMb = 65536;
    static constexpr std::size_t kBucketSize = 4;

    explicit TranspositionTable(std::size_t size_mb = kDefaultSizeMb);

    // Reallocates the table to the largest power-of-two bucket count that fits in `size_mb`
    // mebibytes. The contents are lost. Throws std::bad_alloc when the memory is not available.
    void resize(std::size_t size_mb);

    // Forgets every entry.
    void clear();

    // Marks the start of a new search, so entries of earlier searches are replaced first.
    void new_search() noexcept;

    [[nodiscard]] std::optional<TtEntry> probe(Key key) const noexcept;

    void store(Key key, Move move, Score score, Score eval, int depth, Bound bound) noexcept;

    // Permille of the entries used by the current search, estimated from the first 1000 entries.
    [[nodiscard]] int hashfull() const noexcept;

    [[nodiscard]] std::size_t slot_count() const noexcept { return buckets_.size() * kBucketSize; }

private:
    // Generations count modulo this, in the bits of `bound_generation` the bound leaves free.
    static constexpr unsigned kGenerationShift = 2;
    static constexpr unsigned kGenerationCycle = 1U << (8U - kGenerationShift);

    struct Slot {
        Key key = 0;
        std::uint16_t move = 0;
        std::int16_t score = 0;
        std::int16_t eval = 0;
        std::uint8_t depth = 0;
        // The bound in the low kGenerationShift bits, the generation above them.
        std::uint8_t bound_generation = 0;

        [[nodiscard]] Bound bound() const noexcept {
            return static_cast<Bound>(bound_generation & ((1U << kGenerationShift) - 1));
        }
        [[nodiscard]] unsigned generation() const noexcept {
            return static_cast<unsigned>(bound_generation) >> kGenerationShift;
        }
        [[nodiscard]] bool used() const noexcept { return bound() != Bound::None; }
    };
    static_assert(sizeof(Slot) == 16);

    struct alignas(64) Bucket {
        std::array<Slot, kBucketSize> slots{};
    };
    static_assert(sizeof(Bucket) == 64);

    [[nodiscard]] std::size_t index(Key key) const noexcept {
        return static_cast<std::size_t>(key) & (buckets_.size() - 1);
    }

    // How many searches ago `slot` was stored, modulo kGenerationCycle.
    [[nodiscard]] unsigned age(const Slot& slot) const noexcept {
        return (generation_ - slot.generation()) % kGenerationCycle;
    }

    std::vector<Bucket> buckets_;
    unsigned generation_ = 0;
};

}  // namespace chess
