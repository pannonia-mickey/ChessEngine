#include "search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
// At most this share of the clock left after the move overhead is spent on one move.
constexpr int kMaxClockUsagePercent = 75;
// How often (in nodes) the clock and the stop request are polled.
constexpr std::uint64_t kCheckInterval = 1024;
// The deepest ply a node can be at; evaluated statically, never expanded.
constexpr auto kLastPly = static_cast<std::size_t>(kMaxPly - 1);
// Null move pruning is tried from this remaining depth on.
constexpr int kNullMoveMinDepth = 3;
// History scores stay within [-kMaxHistory, kMaxHistory], below the killer move scores.
constexpr int kMaxHistory = 16'384;
// Late move reductions apply from this remaining depth on, to moves after this many searched ones.
constexpr int kLmrMinDepth = 3;
constexpr int kLmrMinMoves = 3;

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
    // Never plan to use the whole clock: a late reply on a busy machine would lose on time.
    const milliseconds max_usage = std::max(available * kMaxClockUsagePercent / 100, 1ms);
    const milliseconds soft =
        std::min(available / moves_to_go + limits.increment[us] * 3 / 4, max_usage);
    const milliseconds hard = std::min(soft * 3, max_usage);
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

// Without pieces other than pawns, zugzwang is common and passing the turn is no safe bound.
bool has_non_pawn_material(const Position& pos, Color color) {
    return (pos.pieces(color) & ~(pos.pieces(Pawn) | pos.pieces(King))) != 0;
}

bool is_capture(const Position& pos, Move move) {
    return move.type() == MoveType::EnPassant || pos.piece_on(move.to()) != NoPiece;
}

// Neither a capture nor a promotion: the moves ordered by killers and history.
bool is_quiet(const Position& pos, Move move) {
    return !is_capture(pos, move) && move.type() != MoveType::Promotion;
}

// Captures and queen promotions: the moves quiescence search looks at.
bool is_tactical(const Position& pos, Move move) {
    return is_capture(pos, move) ||
           (move.type() == MoveType::Promotion && move.promotion() == Queen);
}

// How many plies late move reductions take off a quiet move, by remaining depth and by how many
// moves were searched before it. Grows with the logarithm of both: the later a move comes in a
// well ordered list and the deeper the search, the less likely the move is to matter.
int lmr_reduction(int depth, int move_number) {
    constexpr std::size_t kTableSize = 64;
    static const auto reductions = [] {
        std::array<std::array<int, kTableSize>, kTableSize> table{};
        for (std::size_t d = 1; d < kTableSize; ++d) {
            for (std::size_t m = 1; m < kTableSize; ++m) {
                table[d][m] = static_cast<int>(0.75 + (std::log(static_cast<double>(d)) *
                                                       std::log(static_cast<double>(m)) / 2.25));
            }
        }
        return table;
    }();
    const auto index = [](int value) {
        return std::min(static_cast<std::size_t>(std::max(value, 0)), kTableSize - 1);
    };
    return reductions[index(depth)][index(move_number)];
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

// The two most recent quiet moves that caused a beta cutoff at a ply, newest first. Sibling
// positions tend to be refuted by the same move.
using Killers = std::array<Move, 2>;

// How often each quiet move (by side to move, from and to square) caused a beta cutoff, weighted
// by depth and decreased when it failed to.
class History {
public:
    [[nodiscard]] int get(Color us, Move move) const { return table_[us][move.from()][move.to()]; }

    // Adds `bonus` (negative for a penalty), scaled down as the entry nears kMaxHistory so that
    // entries stay bounded and recent results outweigh old ones.
    void update(Color us, Move move, int bonus) {
        int& entry = table_[us][move.from()][move.to()];
        const int clamped = std::clamp(bonus, -kMaxHistory, kMaxHistory);
        entry += clamped - (entry * std::abs(clamped) / kMaxHistory);
    }

private:
    std::array<std::array<std::array<int, kSquareCount>, kSquareCount>, kColorCount> table_{};
};

class MovePicker {
public:
    // Orders the hash/PV move first, then captures by MVV-LVA (most valuable victim, least
    // valuable attacker) and promotions, then the killer moves, then the other quiet moves by
    // history score. With `tactical_only`, only captures and queen promotions are returned.
    void reset(const Position& pos, const MoveList& moves, Move hash_move, const Killers& killers,
               const History* history, bool tactical_only = false) {
        size_ = 0;
        current_ = 0;
        for (const Move move : moves) {
            if (tactical_only && !is_tactical(pos, move)) {
                continue;
            }
            moves_[size_] = move;
            scores_[size_] = score(pos, move, hash_move, killers, history);
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
    static int score(const Position& pos, Move move, Move hash_move, const Killers& killers,
                     const History* history) {
        constexpr int kHashBonus = 1'000'000;
        constexpr int kTacticalBonus = 100'000;
        constexpr int kFirstKillerBonus = 90'000;
        constexpr int kSecondKillerBonus = 80'000;
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
        if (value != 0) {
            return value;
        }
        if (move == killers[0]) {
            return kFirstKillerBonus;
        }
        if (move == killers[1]) {
            return kSecondKillerBonus;
        }
        return history != nullptr ? history->get(pos.side_to_move(), move) : 0;
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
          start_(limits.start),
          budget_(plan_time(limits, pos.side_to_move())) {}

    SearchResult run(const InfoCallback& on_info) {
        SearchResult result;
        tt_.new_search();
        const int max_depth = std::clamp(limits_.depth, 1, kMaxPly - 1);
        for (int depth = 1; depth <= max_depth; ++depth) {
            selective_depth_ = 0;
            const Score score = negamax(depth, -kInfiniteScore, kInfiniteScore, 0, true, false);
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
                (budget_.soft.has_value() &&
                 (elapsed() >= *budget_.soft || mate_found(score, depth)))) {
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
    // `on_pv` is set along the previous iteration's principal variation, whose moves are tried
    // first. A PV node, whose exact score matters, is one searched with an open window instead:
    // every node of the null window searches is a non-PV node, while a node off the previous PV
    // can be a PV node when the search finds a new best line through it.
    // `after_null` is set right after a null move, so two are never made in a row.
    // NOLINTNEXTLINE(misc-no-recursion): recursion depth is bounded by kMaxPly.
    Score negamax(int depth, Score alpha, Score beta, std::size_t ply, bool on_pv,
                  bool after_null) {
        pv_length_[ply] = 0;
        if (depth <= 0) {
            return quiescence(alpha, beta, ply);
        }
        const bool pv_node = beta - alpha > 1;
        if (count_node()) {
            return 0;
        }
        if (ply > 0 && (pos_.is_repetition(ply) || insufficient_material(pos_))) {
            return kDrawScore;
        }
        if (ply >= kLastPly) {
            return evaluate(pos_);
        }

        const Key key = pos_.key();
        Move tt_move = Move::null();
        if (const auto entry = tt_.probe(key)) {
            tt_move = entry->move;
            // PV nodes always search, so the root knows a best move and the principal variation
            // is not cut short. A position the 50-move rule may already have drawn is searched
            // too, since the entry cannot know about the rule.
            if (!pv_node && entry->depth >= depth && pos_.halfmove_clock() < 100) {
                const Score score = score_from_tt(entry->score, ply);
                if (entry->bound == Bound::Exact ||
                    (entry->bound == Bound::Lower && score >= beta) ||
                    (entry->bound == Bound::Upper && score <= alpha)) {
                    return score;
                }
            }
        }

        PlyData& data = stack_[ply];
        const MoveList& moves = data.moves;
        generate_legal_moves(pos_, data.moves);
        const bool in_check = pos_.in_check();
        if (moves.empty()) {
            return in_check ? mated_score(ply) : kDrawScore;
        }
        if (ply > 0 && pos_.halfmove_clock() >= 100) {
            return kDrawScore;
        }

        // Null move pruning: if passing the turn still fails high in a reduced search, a real
        // move almost surely would too. Skipped in check, in PV nodes, and without pieces
        // (zugzwang).
        if (!pv_node && !after_null && !in_check && depth >= kNullMoveMinDepth &&
            beta < kMateBound && has_non_pawn_material(pos_, pos_.side_to_move()) &&
            evaluate(pos_) >= beta) {
            const int reduction = 3 + (depth / 4);
            pos_.make_null_move();
            const Score score =
                -negamax(depth - 1 - reduction, -beta, -beta + 1, ply + 1, false, true);
            pos_.unmake_null_move();
            if (aborted_) {
                return 0;
            }
            if (score >= beta) {
                // An unproven mate from a null move search is not trusted.
                return score >= kMateBound ? beta : score;
            }
        }

        // The previous iteration's PV is followed first along the PV; elsewhere the table's move.
        const Move pv_move = on_pv && ply < previous_pv_length_ ? previous_pv_[ply] : Move::null();
        // The killers two plies down were found below other siblings of this node's children.
        if (ply + 2 <= kLastPly) {
            killers_[ply + 2] = {};
        }
        MovePicker& picker = data.picker;
        picker.reset(pos_, moves, pv_move.is_null() ? tt_move : pv_move, killers_[ply], &history_);
        const Score original_alpha = alpha;
        Score best = -kInfiniteScore;
        Move best_move = Move::null();
        // Quiet moves searched before the current one, penalized when another move cuts off.
        MoveList& quiets_tried = data.quiets_tried;
        quiets_tried.clear();
        int moves_searched = 0;
        while (const auto move = picker.next()) {
            const bool quiet = is_quiet(pos_, *move);
            const bool killer = *move == killers_[ply][0] || *move == killers_[ply][1];
            const bool child_on_pv = on_pv && *move == pv_move;
            pos_.make_move(*move);
            const bool gives_check = pos_.in_check();
            Score score = 0;
            if (moves_searched == 0) {
                score = -negamax(depth - 1, -beta, -alpha, ply + 1, child_on_pv, false);
            } else {
                // Principal variation search: with good ordering the first move is the best, so
                // the others only need proving worse, which a null window around alpha does more
                // cheaply. A move that beats alpha anyway is searched again with the full window.
                // Late move reductions: quiet moves late in the ordering rarely matter, so their
                // null window search is shallower at first, and at full depth only if they beat
                // alpha. Captures, promotions, killers, checks and check evasions are never
                // reduced.
                bool full_depth = true;
                if (depth >= kLmrMinDepth && moves_searched >= kLmrMinMoves && quiet && !killer &&
                    !in_check && !gives_check) {
                    int reduction = lmr_reduction(depth, moves_searched);
                    if (pv_node) {
                        --reduction;
                    }
                    reduction = std::clamp(reduction, 0, depth - 2);
                    if (reduction > 0) {
                        score = -negamax(depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, false,
                                         false);
                        full_depth = score > alpha && !aborted_;
                    }
                }
                if (full_depth) {
                    score = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, false, false);
                    if (score > alpha && score < beta && !aborted_) {
                        score = -negamax(depth - 1, -beta, -alpha, ply + 1, child_on_pv, false);
                    }
                }
            }
            pos_.unmake_move();
            ++moves_searched;
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
                        if (quiet) {
                            update_quiet_stats(*move, quiets_tried, depth, ply);
                        }
                        break;
                    }
                }
            }
            if (quiet) {
                quiets_tried.push_back(*move);
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

        PlyData& data = stack_[ply];
        generate_legal_moves(pos_, data.moves);
        if (in_check && data.moves.empty()) {
            return mated_score(ply);
        }

        MovePicker& picker = data.picker;
        // Out of check only captures and queen promotions are searched; the rest are not even
        // scored.
        picker.reset(pos_, data.moves, Move::null(), {}, nullptr, !in_check);
        while (const auto move = picker.next()) {
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

    // Rewards the quiet move that caused a beta cutoff and penalizes the quiet moves searched
    // before it in vain.
    void update_quiet_stats(Move cutoff_move, const MoveList& quiets_tried, int depth,
                            std::size_t ply) {
        Killers& killers = killers_[ply];
        if (killers[0] != cutoff_move) {
            killers[1] = killers[0];
            killers[0] = cutoff_move;
        }
        const Color us = pos_.side_to_move();
        const int bonus = depth * depth;
        history_.update(us, cutoff_move, bonus);
        for (const Move move : quiets_tried) {
            history_.update(us, move, -bonus);
        }
    }

    void update_pv(std::size_t ply, Move move) {
        auto& line = pv_[ply];
        const auto& child = pv_[ply + 1];
        line[0] = move;
        const std::size_t child_length = pv_length_[ply + 1];
        std::copy_n(child.begin(), child_length, std::next(line.begin()));
        pv_length_[ply] = child_length + 1;
    }

    // Whether an iteration to `depth` proved a mate no deeper than that. Searching deeper could
    // only find a shorter one, so a search on the clock saves its time instead.
    static bool mate_found(Score score, int depth) {
        return score >= kMateBound && kMateScore - score <= depth;
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

    std::array<Killers, kMaxPly + 1> killers_{};
    History history_;

    // The move lists of the node being searched at each ply. A node's lists are only used while
    // it is on the search path, and only one node per ply is, so they live here rather than in
    // the recursive functions' stack frames, keeping the search thread's stack small.
    struct PlyData {
        MoveList moves;
        MoveList quiets_tried;
        MovePicker picker;
    };
    std::array<PlyData, kMaxPly + 1> stack_{};
};

}  // namespace

SearchResult search(Position& pos, const SearchLimits& limits, TranspositionTable& tt,
                    std::stop_token stop, const InfoCallback& on_info) {
    // The PV tables are large, so the searcher lives on the heap rather than the stack.
    const auto searcher = std::make_unique<Searcher>(pos, limits, tt, std::move(stop));
    return searcher->run(on_info);
}

}  // namespace chess
