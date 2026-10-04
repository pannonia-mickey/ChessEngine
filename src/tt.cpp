#include "tt.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace chess {

TranspositionTable::TranspositionTable(std::size_t size_mb) {
    resize(size_mb);
}

void TranspositionTable::resize(std::size_t size_mb) {
    constexpr std::size_t kMebibyte = std::size_t{1} << 20U;
    const std::size_t bytes = std::clamp(size_mb, kMinSizeMb, kMaxSizeMb) * kMebibyte;
    // A power-of-two slot count lets the low bits of the key select the slot.
    const std::size_t count = std::bit_floor(bytes / sizeof(Slot));
    // Release the old table first, so peak memory is the new size rather than the sum of both.
    slots_ = {};
    slots_.resize(count);
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::ranges::fill(slots_, Slot{});
    generation_ = 0;
}

void TranspositionTable::new_search() noexcept {
    ++generation_;
}

std::optional<TtEntry> TranspositionTable::probe(Key key) const noexcept {
    const Slot& slot = slots_[index(key)];
    if (slot.key != key || slot.bound == Bound::None) {
        return std::nullopt;
    }
    return TtEntry{.move = Move::from_raw(slot.move),
                   .score = slot.score,
                   .depth = slot.depth,
                   .bound = slot.bound};
}

void TranspositionTable::store(Key key, Move move, Score score, int depth, Bound bound) noexcept {
    Slot& slot = slots_[index(key)];
    const bool same_position = slot.key == key && slot.bound != Bound::None;
    if (same_position && slot.generation == generation_ && bound != Bound::Exact &&
        depth < slot.depth) {
        return;
    }
    // A fail-low search finds no best move; keep the one an earlier search found.
    if (move.is_null() && same_position) {
        move = Move::from_raw(slot.move);
    }
    slot = {.key = key,
            .move = move.raw(),
            .score = static_cast<std::int16_t>(score),
            .depth = static_cast<std::uint8_t>(std::max(depth, 0)),
            .bound = bound,
            .generation = generation_};
}

int TranspositionTable::hashfull() const noexcept {
    const std::size_t sample = std::min<std::size_t>(1000, slots_.size());
    const auto used =
        std::count_if(slots_.begin(), slots_.begin() + static_cast<std::ptrdiff_t>(sample),
                      [this](const Slot& slot) {
                          return slot.bound != Bound::None && slot.generation == generation_;
                      });
    return static_cast<int>(used * 1000 / static_cast<std::ptrdiff_t>(sample));
}

}  // namespace chess
