#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

#include "bitboard.hpp"
#include "movegen.hpp"

namespace chess {
namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using namespace std::chrono_literals;

// Sudden death time controls are planned as if this many moves were left.
constexpr int kDefaultMovesToGo = 30;
// How often (in nodes) the clock and the stop request are polled.
constexpr std::uint64_t kCheckInterval = 1024;
// The deepest ply a node can be at; evaluated statically, never expanded.
constexpr auto kLastPly = static_cast<std::size_t>(kMaxPly - 1);

struct TimeBudget {
    // No new iteration starts after this.
    std::optional<milliseconds> soft;
    // The search is aborted at this point, even mid-iteration.
    std::optional<milliseconds> hard;
};

TimeBudget plan_time(const SearchLimits& limits, Color us) {
    if (limits.infinite) {
        return {};
    }
    if (limits.move_time.has_value()) {
        const milliseconds budget = std::max(*limits.move_time - limits.move_overhead, 1ms);
        return {.soft = budget, .hard = budget};
    }
    const std::optional<milliseconds> clock = limits.time[us];
    if (!clock.has_value()) {
        return {};
    }
    const milliseconds available = std::max(*clock - limits.move_overhead, 1ms);
    const int moves_to_go = limits.moves_to_go > 0 ? std::min(limits.moves_to_go, kDefaultMovesToGo)
                                                   : kDefaultMovesToGo;
    const milliseconds soft =
        std::min(available / moves_to_go + limits.increment[us] * 3 / 4, available);
    const milliseconds hard = std::min(soft * 3, available);
    return {.soft = soft, .hard = hard};
}

// Kings alone, or a king and a single minor piece against a bare king: no mate is possible.
bool insufficient_material(const Position& pos) {
    const Bitboard pieces = pos.pieces();
    if (popcount(pieces) > 3) {
        return false;
    }
    return (pos.pieces(Pawn) | pos.pieces(Rook) | pos.pieces(Queen)) == 0;
}

bool is_capture(const Position& pos, Move move) {
    return move.type() == MoveType::EnPassant || pos.piece_on(move.to()) != NoPiece;
}

// Captures and queen promotions: the moves quiescence search looks at.
bool is_tactical(const Position& pos, Move move) {
    return is_capture(pos, move) ||
           (move.type() == MoveType::Promotion && move.promotion() == Queen);
}

// Mate scores count plies from the root, but a table entry may be reached at a different ply.
// The table stores them counted from the node instead.
Score score_to_tt(Score score, std::size_t ply) {
    const auto plies = static_cast<Score>(ply);
    if (score >= kMateBound) {
        return score + plies;
    }
    if (score <= -kMateBound) {
        return score - plies;
    }
    return score;
}

Score score_from_tt(Score score, std::size_t ply) {
    const auto plies = static_cast<Score>(ply);
    if (score >= kMateBound) {
        return score - plies;
    }
    if (score <= -kMateBound) {
        return score + plies;
    }
    return score;
}

class MovePicker {
public:
    // Orders the hash/PV move first, then captures by MVV-LVA (most valuable victim, least
    // valuable attacker) and promotions, then quiet moves in generation order.
    MovePicker(const Position& pos, const MoveList& moves, Move hash_move) {
        for (const Move move : moves) {
            moves_[size_] = move;
            scores_[size_] = score(pos, move, hash_move);
            ++size_;
        }
    }

    // The highest-scored move not returned yet, or nullopt when all have been.
    std::optional<Move> next() {
        if (current_ == size_) {
            return std::nullopt;
        }
        std::size_t best = current_;
        for (std::size_t i = current_ + 1; i < size_; ++i) {
            if (scores_[i] > scores_[best]) {
                best = i;
            }
        }
        std::swap(moves_[current_], moves_[best]);
        std::swap(scores_[current_], scores_[best]);
        return moves_[current_++];
    }

private:
    static int score(const Position& pos, Move move, Move hash_move) {
        constexpr int kHashBonus = 1'000'000;
        constexpr int kTacticalBonus = 100'000;
        if (move == hash_move) {
            return kHashBonus;
        }
        int value = 0;
        if (is_capture(pos, move)) {
            const PieceType victim =
                move.type() == MoveType::EnPassant ? Pawn : type_of(pos.piece_on(move.to()));
            const PieceType attacker = type_of(pos.piece_on(move.from()));
            value += kTacticalBonus + (piece_value(victim) * 8) - static_cast<int>(attacker);
        }
        if (move.type() == MoveType::Promotion) {
            value += kTacticalBonus + piece_value(move.promotion());
        }
        return value;
    }

    std::array<Move, MoveList::kCapacity> moves_{};
    std::array<int, MoveList::kCapacity> scores_{};
    std::size_t size_ = 0;
    std::size_t current_ = 0;
};

class Searcher {
public:
    Searcher(Position& pos, const SearchLimits& limits, TranspositionTable& tt,
             std::stop_token stop)
        : pos_(pos),
          limits_(limits),
          tt_(tt),
          stop_(std::move(stop)),
          start_(Clock::now()),
          budget_(plan_time(limits, pos.side_to_move())) {}

    SearchResult run(const InfoCallback& on_info) {
        SearchResult result;
        tt_.new_search();
        const int max_depth = std::clamp(limits_.depth, 1, kMaxPly - 1);
        for (int depth = 1; depth <= max_depth; ++depth) {
            selective_depth_ = 0;
            const Score score = negamax(depth, -kInfiniteScore, kInfiniteScore, 0, true);
            if (aborted_) {
                break;
            }
            previous_pv_ = pv_[0];
            previous_pv_length_ = pv_length_[0];
            result = {.best_move = previous_pv_length_ > 0 ? previous_pv_[0] : Move::null(),
                      .score = score,
                      .depth = depth,
                      .nodes = nodes_};
            if (on_info) {
                on_info({.depth = depth,
                         .selective_depth = selective_depth_,
                         .score = score,
                         .nodes = nodes_,
                         .elapsed = elapsed(),
                         .hashfull = tt_.hashfull(),
                         .pv = std::span<const Move>(previous_pv_).first(previous_pv_length_)});
            }
            // From here on the search may be interrupted: a move is known.
            can_abort_ = true;
            if (result.best_move.is_null() || stop_.stop_requested() ||
                (budget_.soft.has_value() && elapsed() >= *budget_.soft)) {
                break;
            }
        }
        result.nodes = nodes_;
        if (limits_.infinite) {
            wait_for_stop();
        }
        return result;
    }

private:
    // NOLINTNEXTLINE(misc-no-recursion): recursion depth is bounded by kMaxPly.
    Score negamax(int depth, Score alpha, Score beta, std::size_t ply, bool on_pv) {
        pv_length_[ply] = 0;
        if (depth <= 0) {
            return quiescence(alpha, beta, ply);
        }
        if (count_node()) {
            return 0;
        }
        if (ply > 0 && (pos_.is_repetition() || insufficient_material(pos_))) {
            return kDrawScore;
        }
        if (ply >= kLastPly) {
            return evaluate(pos_);
        }

        const Key key = pos_.key();
        Move tt_move = Move::null();
        if (const auto entry = tt_.probe(key)) {
            tt_move = entry->move;
            // The root always searches, so a best move is known. A position the 50-move rule may
            // already have drawn is searched too, since the entry cannot know about the rule.
            if (ply > 0 && entry->depth >= depth && pos_.halfmove_clock() < 100) {
                const Score score = score_from_tt(entry->score, ply);
                if (entry->bound == Bound::Exact ||
                    (entry->bound == Bound::Lower && score >= beta) ||
                    (entry->bound == Bound::Upper && score <= alpha)) {
                    return score;
                }
            }
        }

        const MoveList moves = generate_legal_moves(pos_);
        if (moves.empty()) {
            return pos_.in_check() ? mated_score(ply) : kDrawScore;
        }
        if (ply > 0 && pos_.halfmove_clock() >= 100) {
            return kDrawScore;
        }

        // The previous iteration's PV is followed first along the PV; elsewhere the table's move.
        const Move pv_move = on_pv && ply < previous_pv_length_ ? previous_pv_[ply] : Move::null();
        MovePicker picker(pos_, moves, pv_move.is_null() ? tt_move : pv_move);
        const Score original_alpha = alpha;
        Score best = -kInfiniteScore;
        Move best_move = Move::null();
        while (const auto move = picker.next()) {
            pos_.make_move(*move);
            const Score score =
                -negamax(depth - 1, -beta, -alpha, ply + 1, on_pv && *move == pv_move);
            pos_.unmake_move();
            if (aborted_) {
                return 0;
            }
            if (score > best) {
                best = score;
                if (score > alpha) {
                    alpha = score;
                    best_move = *move;
                    update_pv(ply, *move);
                    if (alpha >= beta) {
                        break;
                    }
                }
            }
        }

        Bound bound = Bound::Upper;
        if (best >= beta) {
            bound = Bound::Lower;
        } else if (best > original_alpha) {
            bound = Bound::Exact;
        }
        tt_.store(key, best_move, score_to_tt(best, ply), depth, bound);
        return best;
    }

    // Resolves captures until the position is quiet, so the static evaluation is not taken in
    // the middle of an exchange. In check every evasion is searched instead.
    // NOLINTNEXTLINE(misc-no-recursion): recursion depth is bounded by kMaxPly.
    Score quiescence(Score alpha, Score beta, std::size_t ply) {
        pv_length_[ply] = 0;
        if (count_node()) {
            return 0;
        }
        selective_depth_ = std::max(selective_depth_, static_cast<int>(ply));
        if (ply >= kLastPly) {
            return evaluate(pos_);
        }

        const bool in_check = pos_.in_check();
        Score best = -kInfiniteScore;
        if (!in_check) {
            // Standing pat: the side to move can usually do at least as well as doing nothing.
            best = evaluate(pos_);
            if (best >= beta) {
                return best;
            }
            alpha = std::max(alpha, best);
        }

        const MoveList moves = generate_legal_moves(pos_);
        if (in_check && moves.empty()) {
            return mated_score(ply);
        }

        MovePicker picker(pos_, moves, Move::null());
        while (const auto move = picker.next()) {
            if (!in_check && !is_tactical(pos_, *move)) {
                continue;
            }
            pos_.make_move(*move);
            const Score score = -quiescence(-beta, -alpha, ply + 1);
            pos_.unmake_move();
            if (aborted_) {
                return 0;
            }
            if (score > best) {
                best = score;
                if (score > alpha) {
                    alpha = score;
                    update_pv(ply, *move);
                    if (alpha >= beta) {
                        break;
                    }
                }
            }
        }
        return best;
    }

    void update_pv(std::size_t ply, Move move) {
        auto& line = pv_[ply];
        const auto& child = pv_[ply + 1];
        line[0] = move;
        const std::size_t child_length = pv_length_[ply + 1];
        std::copy_n(child.begin(), child_length, std::next(line.begin()));
        pv_length_[ply] = child_length + 1;
    }

    static Score mated_score(std::size_t ply) { return -kMateScore + static_cast<Score>(ply); }

    // Counts a node and returns true when the search must be aborted.
    bool count_node() {
        ++nodes_;
        if (!can_abort_) {
            return false;
        }
        if (limits_.nodes != 0 && nodes_ >= limits_.nodes) {
            aborted_ = true;
        } else if (nodes_ % kCheckInterval == 0) {
            aborted_ =
                stop_.stop_requested() || (budget_.hard.has_value() && elapsed() >= *budget_.hard);
        }
        return aborted_;
    }

    [[nodiscard]] milliseconds elapsed() const {
        return std::chrono::duration_cast<milliseconds>(Clock::now() - start_);
    }

    void wait_for_stop() {
        std::mutex mutex;
        std::condition_variable_any stopped;
        std::unique_lock lock(mutex);
        stopped.wait(lock, stop_, [] { return false; });
    }

    Position& pos_;
    const SearchLimits& limits_;
    TranspositionTable& tt_;
    std::stop_token stop_;
    Clock::time_point start_;
    TimeBudget budget_;

    std::uint64_t nodes_ = 0;
    int selective_depth_ = 0;
    bool can_abort_ = false;
    bool aborted_ = false;

    // pv_[ply] holds the best line found from ply onwards (triangular PV table).
    std::array<std::array<Move, kMaxPly + 1>, kMaxPly + 1> pv_{};
    std::array<std::size_t, kMaxPly + 1> pv_length_{};
    // The principal variation of the last completed iteration, searched first in the next one.
    std::array<Move, kMaxPly + 1> previous_pv_{};
    std::size_t previous_pv_length_ = 0;
};

}  // namespace

SearchResult search(Position& pos, const SearchLimits& limits, TranspositionTable& tt,
                    std::stop_token stop, const InfoCallback& on_info) {
    // The PV tables are large, so the searcher lives on the heap rather than the stack.
    const auto searcher = std::make_unique<Searcher>(pos, limits, tt, std::move(stop));
    return searcher->run(on_info);
}

}  // namespace chess
