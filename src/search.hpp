#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>

#include "evaluate.hpp"
#include "move.hpp"
#include "position.hpp"
#include "tt.hpp"

namespace chess {

inline constexpr int kMaxPly = 128;
inline constexpr Score kMateScore = 32000;
inline constexpr Score kInfiniteScore = kMateScore + 1;
// Scores at least this large in absolute value are mates, found within kMaxPly plies.
inline constexpr Score kMateBound = kMateScore - kMaxPly;

// What "go" asked for. Unset fields do not limit the search.
struct SearchLimits {
    std::array<std::optional<std::chrono::milliseconds>, kColorCount> time{};
    std::array<std::chrono::milliseconds, kColorCount> increment{};
    int moves_to_go = 0;
    std::optional<std::chrono::milliseconds> move_time;
    int depth = kMaxPly - 1;
    std::uint64_t nodes = 0;  // 0 means no node limit.
    // Do not report a best move before being stopped, even when the depth limit is reached.
    bool infinite = false;
    // Pondering ("go ponder") while this points to true: the clock is ignored and no best move is
    // reported before a stop. Once it turns false ("ponderhit"), the search goes on as a normal
    // one on the clock, whose time is measured from then.
    const std::atomic<bool>* pondering = nullptr;
    // Time kept in reserve per move for communication and process scheduling delays.
    std::chrono::milliseconds move_overhead{30};
    // Time is measured from here; by default, from when the limits were made for "go".
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

// Progress of the search after each completed iteration, and after an interrupted one whose best
// move is played instead.
struct SearchInfo {
    int depth = 0;
    int selective_depth = 0;
    Score score = 0;
    // The true score may be higher: reported for an interrupted iteration.
    bool lower_bound = false;
    std::uint64_t nodes = 0;
    std::chrono::milliseconds elapsed{0};
    // Permille of the transposition table filled by this search.
    int hashfull = 0;
    std::span<const Move> pv;
};

struct SearchResult {
    // The last completed iteration's best move, or a better root move that an interrupted later
    // iteration searched to the end; score and depth describe the last completed iteration.
    // The null move when the side to move has no legal move.
    Move best_move;
    Score score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
};

using InfoCallback = std::function<void(const SearchInfo&)>;

// Searches the position with iterative deepening alpha-beta until a limit is reached or a stop
// is requested. The first iteration always completes, so a legal move is returned whenever one
// exists. `pos` is restored before returning. Results are read from and written to `tt`, which
// may hold entries from earlier searches.
[[nodiscard]] SearchResult search(Position& pos, const SearchLimits& limits, TranspositionTable& tt,
                                  std::stop_token stop = {}, const InfoCallback& on_info = {});

// Moves until mate (negative when getting mated) for a mate score, as UCI's "score mate" wants.
[[nodiscard]] constexpr int mate_in_moves(Score score) noexcept {
    return score > 0 ? (kMateScore - score + 1) / 2 : -(kMateScore + score) / 2;
}

[[nodiscard]] constexpr bool is_mate_score(Score score) noexcept {
    return score >= kMateBound || score <= -kMateBound;
}

}  // namespace chess
