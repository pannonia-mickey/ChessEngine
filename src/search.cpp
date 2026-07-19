#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include "eval.hpp"
#include "movegen.hpp"
#include "position.hpp"
#include "see.hpp"
#include "tt.hpp"

namespace chess {

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

// Cheap MVV-LVA ordering key: captures score highest (bigger victim, smaller
// attacker is better). Used only to make alpha-beta cutoffs more effective;
// not a correctness requirement.
int mvv_lva_score(const Position& pos, Move m) {
    if (!is_capture(pos, m)) return -1;
    Piece captured = pos.piece_on(to_sq(m));
    if (captured == NO_PIECE) return PIECE_VALUE[PAWN] * 10 - PIECE_VALUE[PAWN]; // en passant
    Piece attacker = pos.piece_on(from_sq(m));
    return PIECE_VALUE[type_of(captured)] * 10 - PIECE_VALUE[type_of(attacker)];
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

// Ordering priority shared by order_moves() and score_moves(): `pv_move`
// first, then captures by MVV-LVA (offset above the killer/history tiers so
// a real capture is never ordered behind a quiet killer), then the two
// killer moves for this ply, then history score.
int move_order_score(const Position& pos, Move m, Move pv_move, Color us, int killer_ply,
                      const SearchTables& tables) {
    if (m == pv_move) return PV_SCORE;
    if (is_capture(pos, m)) {
        int capture_score = flag_of(m) == PROMOTION ? mvv_lva_score(pos, m) : see(pos, m);
        return CAPTURE_BASE + capture_score;
    }
    if (m == tables.killers[killer_ply][0]) return KILLER1_SCORE;
    if (m == tables.killers[killer_ply][1]) return KILLER2_SCORE;
    return tables.history[us][from_sq(m)][to_sq(m)];
}

// Eagerly sorts every move in `list` by move_order_score(). Used only at the
// root, where every root move is always searched in full (no alpha-beta
// cutoff ever skips the rest of the list there), so there is no benefit to
// deferring the ordering work.
void order_moves(const Position& pos, MoveList& list, Move pv_move, int ply,
                  const SearchTables& tables) {
    int killer_ply = ply <= MAX_DEPTH ? ply : MAX_DEPTH;
    Color us = pos.side_to_move();

    struct ScoredMove { int score; Move move; };
    ScoredMove scored[256];
    for (int i = 0; i < list.size; ++i) {
        Move m = list.moves[i];
        scored[i] = {move_order_score(pos, m, pv_move, us, killer_ply, tables), m};
    }
    std::sort(scored, scored + list.size, [](const ScoredMove& a, const ScoredMove& b) {
        return a.score > b.score;
    });
    for (int i = 0; i < list.size; ++i) list.moves[i] = scored[i].move;
}

// Lazy variant of order_moves(): computes every move's score (O(n)) but
// leaves `list` unsorted. Pairs with pick_next_move() so a caller that often
// stops early - negamax's and quiescence's move loops both break out on a
// beta cutoff, which happens on the first move at most nodes given decent
// move ordering - only pays for ordering the moves it actually examines,
// instead of a full O(n log n) sort of moves it never looks at.
void score_moves(const Position& pos, MoveList& list, Move pv_move, int ply,
                  const SearchTables& tables, int scores[]) {
    int killer_ply = ply <= MAX_DEPTH ? ply : MAX_DEPTH;
    Color us = pos.side_to_move();
    for (int i = 0; i < list.size; ++i)
        scores[i] = move_order_score(pos, list.moves[i], pv_move, us, killer_ply, tables);
}

// Partial-selection-sort step: finds the highest-scoring move among
// list.moves[start .. list.size), swaps it (and its score) into position
// `start`, and returns it. Repeated calls with start = 0, 1, 2, ... visit
// moves in the same highest-score-first order order_moves()'s full sort
// would have produced, but a caller that stops after k picks only does
// O(k * n) work rather than paying for the full O(n log n) sort up front.
Move pick_next_move(MoveList& list, int scores[], int start) {
    int best_i = start;
    for (int i = start + 1; i < list.size; ++i)
        if (scores[i] > scores[best_i]) best_i = i;
    std::swap(list.moves[start], list.moves[best_i]);
    std::swap(scores[start], scores[best_i]);
    return list.moves[start];
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

    // In check: every legal move is a candidate, so `list` itself (ordered
    // in place) is the move set - no need to copy it into a second MoveList,
    // which would copy all 256 fixed array slots regardless of `size`. Scored
    // lazily (see score_moves()/pick_next_move()) since a check-evasion node
    // commonly cuts off after the first move or two.
    MoveList captures;
    MoveList* moves;
    int scores[256];
    if (checked) {
        score_moves(pos, list, MOVE_NONE, ply, tables, scores);
        moves = &list;
    } else {
        struct ScoredCapture { int score; Move move; };
        ScoredCapture scored[256];
        int n = 0;
        for (Move m : list) {
            if (!is_capture(pos, m)) continue;
            int capture_score = flag_of(m) == PROMOTION ? mvv_lva_score(pos, m) : see(pos, m);
            if (flag_of(m) != PROMOTION && capture_score < 0) continue;
            scored[n++] = {capture_score, m};
        }
        std::sort(scored, scored + n, [](const ScoredCapture& a, const ScoredCapture& b) {
            return a.score > b.score;
        });
        for (int i = 0; i < n; ++i) captures.add(scored[i].move);
        moves = &captures;
    }

    StateInfo st;
    for (int i = 0; i < moves->size; ++i) {
        Move m = checked ? pick_next_move(*moves, scores, i) : moves->moves[i];
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

    // Reverse futility pruning (static null-move pruning): at shallow
    // remaining depth, if the static eval already clears beta by more than
    // a per-ply safety margin, the position is comfortably winning enough
    // that searching further is very unlikely to change the outcome, so
    // the node is cut immediately without recursing. Skipped in check (the
    // static eval is unreliable there) and near mate scores (same
    // reasoning as null-move pruning's own guard below). Fail-soft: returns
    // the actual (eval - margin) lower bound rather than the coarser
    // `beta`, consistent with this same move loop's own fail-soft `best`
    // return below, which likewise can exceed `beta` on a cutoff.
    constexpr int RFP_MAX_DEPTH = 8;
    constexpr int RFP_MARGIN = 120;
    if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
        int eval = evaluate(pos);
        int margin = RFP_MARGIN * depth;
        if (eval - margin >= beta)
            return eval - margin;
    }

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

    // Scored lazily (see score_moves()/pick_next_move()): most nodes cut off
    // on a beta cutoff well before the last move, so this avoids paying for
    // a full sort of moves that are never examined.
    int scores[256];
    score_moves(pos, list, tt_move, ply, tables, scores);

    int alpha_orig = alpha;
    int best = -INF;
    Move best_move = list.moves[0];
    StateInfo st;
    for (int move_index = 0; move_index < list.size; ++move_index) {
        Move m = pick_next_move(list, scores, move_index);
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

    int effective_multipv = std::max(1, limits.multi_pv);

    for (int depth = 1; depth <= limits.depth; ++depth) {
        if (limits.stop && limits.stop->load(std::memory_order_relaxed)) break;
        if (tg.active && depth > 1 &&
            std::chrono::steady_clock::now() >= tg.deadline)
            break;

        std::vector<Move> excluded;
        bool depth_aborted = false;

        for (int line = 0; line < effective_multipv; ++line) {
            MoveList line_list;
            for (Move m : root_list)
                if (std::find(excluded.begin(), excluded.end(), m) == excluded.end())
                    line_list.add(m);
            if (line_list.size == 0) break; // fewer legal root moves than requested lines

            // Aspiration windows are only meaningful for line 0, whose score
            // history (prev_score) is what they're guessing around; other
            // lines search the full window every time.
            order_moves(pos, line_list, line == 0 ? prev_best_move : MOVE_NONE, 0, tables);
            tables.seldepth = 0;

            constexpr int ASP_WINDOW = 25;
            int alpha = -INF, beta = INF;
            if (line == 0 && depth >= 4) {
                alpha = prev_score - ASP_WINDOW;
                beta = prev_score + ASP_WINDOW;
            }

            Move line_best_move = line_list.moves[0];
            int line_best_score = -INF;
            bool line_aborted = false;

            while (true) {
                line_best_score = -INF;
                Move iter_best_move = line_list.moves[0];
                int a = alpha;
                StateInfo st;
                for (Move m : line_list) {
                    pos.do_move(m, st);
                    history.push_back(pos.key());
                    int score = -negamax(pos, depth - 1, -beta, -a, 1, total_nodes, tg, tt, tables, history);
                    history.pop_back();
                    pos.undo_move(m, st);
                    if (tg.aborted) { line_aborted = true; break; }

                    if (score > line_best_score) { line_best_score = score; iter_best_move = m; }
                    if (line_best_score > a) a = line_best_score;
                }
                if (line_aborted) break;

                if (line_best_score <= alpha && alpha > -INF) { alpha = -INF; continue; }
                if (line_best_score >= beta && beta < INF) { beta = INF; continue; }
                line_best_move = iter_best_move;
                break;
            }

            if (line_aborted) { depth_aborted = true; break; }

            excluded.push_back(line_best_move);

            if (line == 0) {
                result.best = line_best_move;
                result.score = line_best_score;
                result.depth = depth;
                prev_best_move = line_best_move;
                prev_score = line_best_score;
            }

            if (limits.on_iteration) {
                long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                IterationInfo info;
                info.multipv_index = line;
                info.depth = depth;
                info.seldepth = tables.seldepth;
                info.score = line_best_score;
                info.nodes = total_nodes;
                info.time_ms = elapsed_ms;
                info.best = line_best_move;
                limits.on_iteration(info);
            }
        }

        if (depth_aborted) break;
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace chess
