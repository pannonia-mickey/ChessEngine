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
    // A power-of-two bucket count lets the low bits of the key select the bucket.
    const std::size_t count = std::bit_floor(bytes / sizeof(Bucket));
    // Release the old table first, so peak memory is the new size rather than the sum of both.
    buckets_ = {};
    buckets_.resize(count);
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::ranges::fill(buckets_, Bucket{});
    generation_ = 0;
}

void TranspositionTable::new_search() noexcept {
    generation_ = (generation_ + 1) % kGenerationCycle;
}

std::optional<TtEntry> TranspositionTable::probe(Key key) const noexcept {
    for (const Slot& slot : buckets_[index(key)].slots) {
        if (slot.key == key && slot.used()) {
            return TtEntry{.move = Move::from_raw(slot.move),
                           .score = slot.score,
                           .eval = slot.eval,
                           .depth = slot.depth,
                           .bound = slot.bound()};
        }
    }
    return std::nullopt;
}

void TranspositionTable::store(Key key, Move move, Score score, Score eval, int depth,
                               Bound bound) noexcept {
    auto& slots = buckets_[index(key)].slots;
    // The entry of this position if there is one, else the one to replace.
    Slot* target = nullptr;
    for (Slot& slot : slots) {
        if (slot.key == key && slot.used()) {
            target = &slot;
            break;
        }
    }
    if (target != nullptr) {
        if (target->generation() == generation_ && depth < target->depth &&
            (bound != Bound::Exact || depth == 0)) {
            return;
        }
        // A fail-low search finds no best move; keep the one an earlier search found.
        if (move.is_null()) {
            move = Move::from_raw(target->move);
        }
    } else {
        // Each search ago counts as this many plies less depth.
        constexpr int kAgePenalty = 8;
        const auto worth = [this](const Slot& slot) {
            return slot.used()
                       ? static_cast<int>(slot.depth) - (kAgePenalty * static_cast<int>(age(slot)))
                       : std::numeric_limits<int>::min();
        };
        target = &*std::ranges::min_element(slots, {}, worth);
    }
    *target = {.key = key,
               .move = move.raw(),
               .score = static_cast<std::int16_t>(score),
               .eval = static_cast<std::int16_t>(eval),
               .depth = static_cast<std::uint8_t>(std::max(depth, 0)),
               .bound_generation = static_cast<std::uint8_t>(static_cast<unsigned>(bound) |
                                                             (generation_ << kGenerationShift))};
}

int TranspositionTable::hashfull() const noexcept {
    constexpr std::size_t kSample = 1000;
    std::size_t sampled = 0;
    std::size_t used = 0;
    for (const Bucket& bucket : buckets_) {
        for (const Slot& slot : bucket.slots) {
            if (slot.used() && slot.generation() == generation_) {
                ++used;
            }
            ++sampled;
        }
        if (sampled >= kSample) {
            break;
        }
    }
    return sampled == 0 ? 0 : static_cast<int>(used * 1000 / sampled);
}

}  // namespace chess
