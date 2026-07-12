#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

#include "eval.hpp"
#include "movegen.hpp"
#include "position.hpp"
#include "tt.hpp"

namespace chess {

namespace {

constexpr int INF = 1'000'000;
constexpr int MATE_THRESHOLD = MATE - MAX_DEPTH;

int score_to_tt(int score, int ply) {
    if (score >= MATE_THRESHOLD) return score + ply;
    if (score <= -MATE_THRESHOLD) return score - ply;
    return score;
}

int score_from_tt(int score, int ply) {
    if (score >= MATE_THRESHOLD) return score - ply;
    if (score <= -MATE_THRESHOLD) return score + ply;
    return score;
}

bool in_check(const Position& pos) {
    Color us = pos.side_to_move();
    return pos.square_attacked_by(pos.king_square(us), Color(us ^ 1));
}

bool is_capture(const Position& pos, Move m) {
    return pos.piece_on(to_sq(m)) != NO_PIECE || flag_of(m) == EN_PASSANT;
}

// True if the position at the end of `history` (i.e. `history.back()`) is a
// draw by repetition, given `halfmove` = Position::halfmove() for that
// position (how far back a repeat could possibly reach, since a capture or
// pawn move can never repeat) and `ply` = how many of history's trailing
// entries were reached by moves this search itself made (0 at the search
// root; entries before that came from the real game). Identical positions
// are always an even number of plies apart (each ply flips the side to
// move), so candidates are checked 2 apart, starting 4 plies back (the
// minimum possible repeat distance).
//
// A match found strictly inside the search's own moves is trusted alone:
// the search is already choosing to walk back into it, so a second
// repetition is assumed forceable by symmetry. A match anchored in real
// game history needs a second such match before it counts, matching the
// actual threefold-repetition rule (three total occurrences, not two).
bool is_repetition(const std::vector<zobrist::Key>& history, int halfmove, int ply) {
    int end = static_cast<int>(history.size()) - 1;
    int root = end - ply;
    int limit = end - std::min(halfmove, end);
    int occurrences = 0;
    for (int i = end - 4; i >= limit; i -= 2) {
        if (history[i] != history[end]) continue;
        if (i > root) return true;
        if (++occurrences >= 2) return true;
    }
    return false;
}

// Cheap MVV-LVA ordering key: captures score highest (bigger victim, smaller
// attacker is better). Used only to make alpha-beta cutoffs more effective;
// not a correctness requirement.
int mvv_lva_score(const Position& pos, Move m) {
    static constexpr int value[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 20000};
    if (!is_capture(pos, m)) return -1;
    Piece captured = pos.piece_on(to_sq(m));
    if (captured == NO_PIECE) return value[PAWN] * 10 - value[PAWN]; // en passant
    Piece attacker = pos.piece_on(from_sq(m));
    return value[type_of(captured)] * 10 - value[type_of(attacker)];
}

// Mutable move-ordering state for one search_best_move() call. Killers are
// indexed [ply][0..1] (two quiet moves per ply that most recently caused a
// beta cutoff there); sized MAX_DEPTH+1 because negamax's ply can reach
// MAX_DEPTH itself at the last node before hitting depth==0 (ply = 1 + (root
// depth - remaining depth), so at remaining depth 1 with a full-depth root of
// MAX_DEPTH, ply == MAX_DEPTH). History is indexed [color][from][to] and
// accumulates depth^2 per cutoff, biasing towards moves that keep paying off
// at higher depths.
struct SearchTables {
    Move killers[MAX_DEPTH + 1][2] = {};
    int history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};
    int seldepth = 0;
};

constexpr int PV_SCORE = 2'000'000;
constexpr int CAPTURE_BASE = 1'000'000;
constexpr int KILLER1_SCORE = 900'000;
constexpr int KILLER2_SCORE = 800'000;

// `pv_move` first, then captures by MVV-LVA (offset above the killer/history
// tiers so a real capture is never ordered behind a quiet killer), then the
// two killer moves for this ply, then the rest ordered by history score.
void order_moves(const Position& pos, MoveList& list, Move pv_move, int ply,
                  const SearchTables& tables) {
    int killer_ply = ply <= MAX_DEPTH ? ply : MAX_DEPTH;
    std::sort(list.begin(), list.end(), [&](Move a, Move b) {
        auto score = [&](Move m) {
            if (m == pv_move) return PV_SCORE;
            if (is_capture(pos, m)) return CAPTURE_BASE + mvv_lva_score(pos, m);
            if (m == tables.killers[killer_ply][0]) return KILLER1_SCORE;
            if (m == tables.killers[killer_ply][1]) return KILLER2_SCORE;
            return tables.history[pos.side_to_move()][from_sq(m)][to_sq(m)];
        };
        return score(a) > score(b);
    });
}

// Wall-clock deadline enforcement for the search. Checked periodically
// (every 2048 nodes) so a slow iteration cannot overshoot the clock and
// cause a time forfeit. Once `aborted` is set, the in-progress iteration's
// result is discarded by the caller.
struct TimeGuard {
    std::chrono::steady_clock::time_point deadline{};
    bool active = false;
    std::atomic<bool>* stop = nullptr;
    unsigned long long nodes_limit = 0;
    bool aborted = false;

    bool expired(std::uint64_t nodes) {
        if (aborted) return true;
        if (nodes_limit && nodes >= nodes_limit) { aborted = true; return true; }
        if ((nodes & 2047) == 0) {
            if (stop && stop->load(std::memory_order_relaxed)) aborted = true;
            else if (active && std::chrono::steady_clock::now() >= deadline) aborted = true;
        }
        return aborted;
    }
};

// Capped: a chain of forced, non-capturing check evasions has no natural
// terminator the way captures do (captures strictly reduce material; a
// repeating check pattern does not), so past this many consecutive check
// extensions quiescence falls back to capture-only/stand-pat even if still
// in check, bounding worst-case recursion depth.
constexpr int MAX_CHECK_EXT = 16;

// Capture-only search extending past the horizon until the position is
// "quiet" (no more captures), so negamax never has to trust a static eval
// mid-exchange. `list` is the caller's already-generated legal move list for
// this node (guaranteed non-empty: negamax checks mate/stalemate before
// calling this). `check_ext` counts consecutive check-evasion extensions
// leading to this call (see MAX_CHECK_EXT).
int quiescence(Position& pos, MoveList& list, int alpha, int beta, int ply,
               std::uint64_t& nodes, TimeGuard& tg, SearchTables& tables,
               std::vector<zobrist::Key>& history, int check_ext = 0) {
    ++nodes;
    if (ply > tables.seldepth) tables.seldepth = ply;
    if (tg.expired(nodes)) return alpha;

    if (pos.halfmove() >= 100 || is_repetition(history, pos.halfmove(), ply))
        return 0;

    // While in check, standing pat is unsound (the check might be a real
    // threat - a fork, a mating net - not just noise), so every legal move
    // is searched as a check evasion instead of filtering to captures only.
    bool checked = in_check(pos) && check_ext < MAX_CHECK_EXT;
    int stand_pat = evaluate(pos);
    if (!checked) {
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    MoveList moves;
    if (checked) {
        moves = list;
    } else {
        for (Move m : list)
            if (is_capture(pos, m)) moves.add(m);
    }
    order_moves(pos, moves, MOVE_NONE, ply, tables);

    StateInfo st;
    for (Move m : moves) {
        pos.do_move(m, st);
        history.push_back(pos.key());
        MoveList child;
        generate_legal(pos, child);
        int score;
        if (child.size == 0) {
            score = in_check(pos) ? -MATE + (ply + 1) : 0;
        } else {
            int next_ext = checked ? check_ext + 1 : 0;
            score = -quiescence(pos, child, -beta, -alpha, ply + 1, nodes, tg, tables, history, next_ext);
        }
        history.pop_back();
        pos.undo_move(m, st);
        if (tg.aborted) return alpha;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// Negamax with alpha-beta pruning. Returns a score from the perspective of
// the side to move at this node. `ply` counts plies from the search root
// (root is ply 0) and is used only for mate scoring. `nodes` is incremented
// once per call to this function (i.e. once per node visited, including
// leaves and terminal mate/stalemate nodes).
int negamax(Position& pos, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes, TimeGuard& tg, TranspositionTable& tt,
            SearchTables& tables, std::vector<zobrist::Key>& history) {
    ++nodes;
    if (ply > tables.seldepth) tables.seldepth = ply;
    if (tg.expired(nodes)) return 0; // value discarded once aborted

    MoveList list;
    generate_legal(pos, list);
    if (list.size == 0)
        return in_check(pos) ? -MATE + ply : 0;

    if (pos.halfmove() >= 100 || is_repetition(history, pos.halfmove(), ply))
        return 0;

    if (depth == 0)
        return quiescence(pos, list, alpha, beta, ply, nodes, tg, tables, history);

    Color us = pos.side_to_move();

    std::uint64_t key = pos.key();
    const TTEntry* entry = tt.probe(key);
    Move tt_move = MOVE_NONE;
    if (entry) {
        tt_move = entry->best;
        if (entry->depth >= depth) {
            int tt_score = score_from_tt(entry->score, ply);
            if (entry->bound == TT_EXACT) return tt_score;
            if (entry->bound == TT_LOWER && tt_score >= beta) return tt_score;
            if (entry->bound == TT_UPPER && tt_score <= alpha) return tt_score;
        }
    }

    bool checked = in_check(pos);

    // Null-move pruning: if we could pass the turn entirely and the
    // opponent still can't beat beta at a reduced depth, our own best move
    // certainly won't need full-depth verification either. Skipped in check
    // (there is no legal "pass"), near mate scores (unreliable), and in
    // pawn-only endgames (zugzwang: passing can be strictly better than any
    // real move, which breaks the whole assumption).
    if (depth >= 3 && !checked && beta < MATE_THRESHOLD &&
        (pos.pieces(us) & ~(pos.pieces(us, PAWN) | pos.pieces(us, KING)))) {
        constexpr int R = 2;
        NullMoveState nst;
        pos.do_null_move(nst);
        history.push_back(pos.key());
        int null_score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1,
                                   nodes, tg, tt, tables, history);
        history.pop_back();
        pos.undo_null_move(nst);
        if (tg.aborted) return 0;
        if (null_score >= beta) return beta;
    }

    order_moves(pos, list, tt_move, ply, tables);

    int alpha_orig = alpha;
    int best = -INF;
    Move best_move = list.moves[0];
    int move_index = 0;
    StateInfo st;
    for (Move m : list) {
        bool capture = is_capture(pos, m);
        MoveFlag mf = flag_of(m);

        pos.do_move(m, st);
        history.push_back(pos.key());
        bool gives_check = in_check(pos);

        int score;
        // Reduce quiet, non-promoting, non-checking moves searched after the
        // first few (they're least likely to be best, per move ordering).
        // If the reduced search still beats alpha, it wasn't actually a bad
        // move, so re-search at full depth before trusting the score - this
        // re-search is what keeps LMR correctness-preserving: a reduction
        // only ever costs extra nodes, never a wrong answer.
        bool do_lmr = depth >= 3 && move_index >= 4 && !capture &&
                      mf != PROMOTION && !gives_check && m != tt_move;
        if (do_lmr) {
            score = -negamax(pos, depth - 2, -alpha - 1, -alpha, ply + 1,
                              nodes, tg, tt, tables, history);
            if (score > alpha)
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                                  nodes, tg, tt, tables, history);
        } else {
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tg, tt, tables, history);
        }
        history.pop_back();
        pos.undo_move(m, st);
        if (tg.aborted) break;

        if (score > best) { best = score; best_move = m; }
        if (best > alpha) alpha = best;
        if (alpha >= beta) {
            if (!capture) {
                if (tables.killers[ply][0] != m) {
                    tables.killers[ply][1] = tables.killers[ply][0];
                    tables.killers[ply][0] = m;
                }
                tables.history[us][from_sq(m)][to_sq(m)] += depth * depth;
            }
            break;
        }
        ++move_index;
    }

    // Partial (aborted) results are the caller's discarded value, not a real
    // search result, so they must not pollute the table.
    if (!tg.aborted) {
        TTBound bound = best <= alpha_orig ? TT_UPPER
                      : best >= beta       ? TT_LOWER
                                           : TT_EXACT;
        tt.store(key, depth, score_to_tt(best, ply), bound, best_move);
    }
    return best;
}

} // namespace

SearchResult search_best_move(Position& pos, const SearchLimits& limits, TranspositionTable& tt) {
    SearchResult result;

    MoveList root_list;
    generate_legal(pos, root_list);
    if (root_list.size == 0) {
        // Game already over at the root: checkmate or stalemate.
        result.best = MOVE_NONE;
        result.score = in_check(pos) ? -MATE : 0;
        result.nodes = 1;
        result.depth = 0;
        return result;
    }

    if (!limits.search_moves.empty()) {
        MoveList filtered;
        for (Move m : root_list)
            if (std::find(limits.search_moves.begin(), limits.search_moves.end(), m) !=
                limits.search_moves.end())
                filtered.add(m);
        if (filtered.size > 0) root_list = filtered;
    }

    auto start_time = std::chrono::steady_clock::now();
    std::uint64_t total_nodes = 0;

    TimeGuard tg;
    tg.stop = limits.stop;
    tg.nodes_limit = limits.nodes_limit;
    if (limits.movetime_ms > 0) {
        tg.active = true;
        tg.deadline = start_time + std::chrono::milliseconds(limits.movetime_ms);
    }

    SearchTables tables;
    Move prev_best_move = MOVE_NONE;

    // The caller's game history should already end at the current root; if
    // it doesn't (e.g. left at its empty default), seed it with just the
    // root so in-search repeats are still detected.
    std::vector<zobrist::Key> history = limits.history;
    if (history.empty() || history.back() != pos.key()) history.push_back(pos.key());

    // Always have a legal move to return, even if depth 1 is interrupted.
    result.best = root_list.moves[0];

    int prev_score = 0;

    for (int depth = 1; depth <= limits.depth; ++depth) {
        if (limits.stop && limits.stop->load(std::memory_order_relaxed)) break;
        if (tg.active && depth > 1 &&
            std::chrono::steady_clock::now() >= tg.deadline)
            break;

        order_moves(pos, root_list, prev_best_move, 0, tables);
        tables.seldepth = 0;

        // Aspiration window: guess the score won't move far from the last
        // iteration's, so a narrow window prunes more at the root. On
        // failure (the true score lies outside the guess), widen to the
        // full open window and re-search that depth once - this makes a bad
        // guess cost extra nodes, never a wrong answer.
        constexpr int ASP_WINDOW = 25;
        int alpha = -INF, beta = INF;
        if (depth >= 4) {
            alpha = prev_score - ASP_WINDOW;
            beta = prev_score + ASP_WINDOW;
        }

        Move best_move = root_list.moves[0];
        int best_score = -INF;
        bool root_aborted = false;

        while (true) {
            best_score = -INF;
            Move iter_best_move = root_list.moves[0];
            int a = alpha;
            StateInfo st;
            for (Move m : root_list) {
                pos.do_move(m, st);
                history.push_back(pos.key());
                int score = -negamax(pos, depth - 1, -beta, -a, 1, total_nodes, tg, tt, tables, history);
                history.pop_back();
                pos.undo_move(m, st);
                if (tg.aborted) { root_aborted = true; break; }

                if (score > best_score) { best_score = score; iter_best_move = m; }
                if (best_score > a) a = best_score;
            }
            if (root_aborted) break;

            if (best_score <= alpha && alpha > -INF) { alpha = -INF; continue; }
            if (best_score >= beta && beta < INF) { beta = INF; continue; }
            best_move = iter_best_move;
            break;
        }

        if (root_aborted) break; // discard the interrupted iteration

        result.best = best_move;
        result.score = best_score;
        result.depth = depth;
        prev_best_move = best_move;
        prev_score = best_score;

        if (limits.on_iteration) {
            long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            IterationInfo info;
            info.multipv_index = 0;
            info.depth = depth;
            info.seldepth = tables.seldepth;
            info.score = best_score;
            info.nodes = total_nodes;
            info.time_ms = elapsed_ms;
            info.best = best_move;
            limits.on_iteration(info);
        }
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace chess
