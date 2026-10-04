#pragma once

#include <cstddef>
#include <cstdint>
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

struct TtEntry {
    Move move;
    Score score = 0;
    int depth = 0;
    Bound bound = Bound::None;
};

// A hash table of search results keyed by the Zobrist key, so positions reached through
// different move orders (transpositions) and positions seen in earlier iterations are not
// searched from scratch.
//
// Each slot holds one entry. A store replaces the slot unless it holds a deeper, non-exact result
// for the same position from the current search. Mate scores are stored as given: the caller
// makes them relative to the node (see search.cpp).
//
// Not thread-safe: one search uses the table at a time.
class TranspositionTable {
public:
    static constexpr std::size_t kDefaultSizeMb = 16;
    static constexpr std::size_t kMinSizeMb = 1;
    static constexpr std::size_t kMaxSizeMb = 65536;

    explicit TranspositionTable(std::size_t size_mb = kDefaultSizeMb);

    // Reallocates the table to the largest power-of-two slot count that fits in `size_mb`
    // mebibytes. The contents are lost. Throws std::bad_alloc when the memory is not available.
    void resize(std::size_t size_mb);

    // Forgets every entry.
    void clear();

    // Marks the start of a new search, so entries of earlier searches are replaced first.
    void new_search() noexcept;

    [[nodiscard]] std::optional<TtEntry> probe(Key key) const noexcept;

    void store(Key key, Move move, Score score, int depth, Bound bound) noexcept;

    // Permille of the slots used by the current search, estimated from the first 1000 slots.
    [[nodiscard]] int hashfull() const noexcept;

    [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }

private:
    struct Slot {
        Key key = 0;
        std::uint16_t move = 0;
        std::int16_t score = 0;
        std::uint8_t depth = 0;
        Bound bound = Bound::None;
        std::uint8_t generation = 0;
    };

    [[nodiscard]] std::size_t index(Key key) const noexcept {
        return static_cast<std::size_t>(key) & (slots_.size() - 1);
    }

    std::vector<Slot> slots_;
    std::uint8_t generation_ = 0;
};

}  // namespace chess
