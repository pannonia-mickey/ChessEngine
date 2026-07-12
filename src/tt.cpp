#include "tt.hpp"

#include <algorithm>

namespace chess {

namespace {
std::size_t round_down_pow2(std::size_t n) {
    std::size_t p = 1;
    while (p * 2 <= n) p *= 2;
    return p;
}
} // namespace

TranspositionTable::TranspositionTable(std::size_t mb) { resize(mb); }

void TranspositionTable::resize(std::size_t mb) {
    std::size_t bytes = mb * 1024 * 1024;
    std::size_t entries = round_down_pow2(std::max<std::size_t>(1, bytes / sizeof(TTEntry)));
    table_.assign(entries, TTEntry{});
    mask_ = entries - 1;
}

void TranspositionTable::clear() {
    std::fill(table_.begin(), table_.end(), TTEntry{});
}

const TTEntry* TranspositionTable::probe(std::uint64_t key) const {
    const TTEntry& e = table_[key & mask_];
    return e.key == key ? &e : nullptr;
}

void TranspositionTable::store(std::uint64_t key, int depth, int score, TTBound bound, Move best) {
    table_[key & mask_] = TTEntry{key, score, depth, bound, best};
}

int TranspositionTable::hashfull() const {
    std::size_t n = std::min<std::size_t>(1000, table_.size());
    std::size_t filled = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (table_[i].bound != TT_NONE) ++filled;
    return static_cast<int>(filled * 1000 / n);
}

} // namespace chess
