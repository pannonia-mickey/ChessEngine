#include "uci.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iterator>
#include <thread>
#include <vector>
#include <string>

#include "types.hpp"
#include "position.hpp"
#include "movegen.hpp"
#include "search.hpp"
#include "tt.hpp"

namespace chess {

namespace {
const std::string kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// A FEN must have exactly one king per side to be usable by the search
// (king_square() relies on there being exactly one). Checks only the
// piece-placement field (the first space-separated token of the FEN).
bool has_one_king_per_side(const std::string& fen) {
    std::string placement = fen.substr(0, fen.find(' '));
    int white_kings = 0, black_kings = 0;
    for (char c : placement) {
        if (c == 'K') ++white_kings;
        else if (c == 'k') ++black_kings;
    }
    return white_kings == 1 && black_kings == 1;
}
} // namespace

std::string move_to_uci(Move m) {
    Square from = from_sq(m);
    Square to = to_sq(m);
    std::string s;
    s += char('a' + file_of(from));
    s += char('1' + rank_of(from));
    s += char('a' + file_of(to));
    s += char('1' + rank_of(to));
    if (flag_of(m) == PROMOTION) {
        char c = 'q';
        switch (promo_type(m)) {
            case KNIGHT: c = 'n'; break;
            case BISHOP: c = 'b'; break;
            case ROOK:   c = 'r'; break;
            case QUEEN:  c = 'q'; break;
            default: break;
        }
        s += c;
    }
    return s;
}

Move uci_to_move(Position& pos, const std::string& s) {
    if (s.size() < 4) return MOVE_NONE;
    // Each coordinate must land in a-h/1-8: out-of-range file/rank deltas
    // (e.g. 'i' - 'a' = 8) can otherwise wrap into a *different*, coincidentally
    // valid square via make_square()'s rank*8+file arithmetic, silently
    // aliasing a malformed token onto an unrelated legal move instead of
    // being rejected as illegal.
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8' ||
        s[2] < 'a' || s[2] > 'h' || s[3] < '1' || s[3] > '8')
        return MOVE_NONE;
    Square from = make_square(s[0] - 'a', s[1] - '1');
    Square to = make_square(s[2] - 'a', s[3] - '1');
    PieceType promo = NO_PIECE_TYPE;
    if (s.size() > 4) {
        switch (s[4]) {
            case 'n': promo = KNIGHT; break;
            case 'b': promo = BISHOP; break;
            case 'r': promo = ROOK; break;
            case 'q': promo = QUEEN; break;
            default: break;
        }
    }

    MoveList list;
    generate_legal(pos, list);
    for (Move m : list) {
        if (from_sq(m) != from || to_sq(m) != to) continue;
        if (flag_of(m) == PROMOTION) {
            if (promo != NO_PIECE_TYPE && promo_type(m) == promo) return m;
        } else if (promo == NO_PIECE_TYPE) {
            return m;
        }
    }
    return MOVE_NONE;
}

SetOption parse_setoption(const std::vector<std::string>& tok) {
    SetOption opt;
    std::size_t i = 1;
    if (i < tok.size() && tok[i] == "name") ++i;

    std::vector<std::string> name_parts;
    while (i < tok.size() && tok[i] != "value") { name_parts.push_back(tok[i]); ++i; }
    for (std::size_t k = 0; k < name_parts.size(); ++k) {
        if (k) opt.name += ' ';
        opt.name += name_parts[k];
    }

    if (i < tok.size() && tok[i] == "value") {
        ++i;
        for (std::size_t k = i; k < tok.size(); ++k) {
            if (k > i) opt.value += ' ';
            opt.value += tok[k];
        }
    }
    return opt;
}

std::vector<std::string> option_lines() {
    return {
        "option name Hash type spin default 16 min 1 max 1024",
        "option name Ponder type check default false",
        "option name MultiPV type spin default 1 min 1 max 256",
    };
}

bool parse_debug_command(const std::vector<std::string>& tok, bool current) {
    if (tok.size() < 2) return current;
    if (tok[1] == "on") return true;
    if (tok[1] == "off") return false;
    return current;
}

std::vector<zobrist::Key> build_game_history(Position& pos, const std::vector<std::string>& moves) {
    std::vector<zobrist::Key> history{pos.key()};
    for (const std::string& s : moves) {
        Move m = uci_to_move(pos, s);
        if (m == MOVE_NONE) break;
        StateInfo st;
        pos.do_move(m, st);
        history.push_back(pos.key());
    }
    return history;
}

std::vector<Move> extract_pv(Position& pos, const TranspositionTable& tt, Move first, int max_len,
                              std::vector<zobrist::Key> history) {
    std::vector<Move> pv;
    std::vector<StateInfo> states;
    Move m = first;
    int ply = 0;
    while (m != MOVE_NONE && static_cast<int>(pv.size()) < max_len) {
        MoveList legal;
        generate_legal(pos, legal);
        bool ok = false;
        for (Move lm : legal) if (lm == m) { ok = true; break; }
        if (!ok) break;

        StateInfo st;
        pos.do_move(m, st);
        states.push_back(st);
        pv.push_back(m);
        history.push_back(pos.key());
        ++ply;

        // The position just reached is already a forced draw: nothing past
        // it is a real continuation, no matter what the TT chain says.
        if (pos.halfmove() >= 100 || is_repetition(history, pos.halfmove(), ply))
            break;

        const TTEntry* e = tt.probe(pos.key());
        m = e ? e->best : MOVE_NONE;
    }
    for (int i = static_cast<int>(pv.size()) - 1; i >= 0; --i)
        pos.undo_move(pv[i], states[i]);
    return pv;
}

GoOptions parse_go(Position& pos, const std::vector<std::string>& tok) {
    GoOptions g;
    for (std::size_t i = 1; i < tok.size(); ++i) {
        if (tok[i] == "depth" && i + 1 < tok.size()) {
            try { g.depth = std::stoi(tok[++i]); g.depth_set = true; }
            catch (const std::exception&) {}
        } else if (tok[i] == "movetime" && i + 1 < tok.size()) {
            try { g.movetime_ms = std::stoll(tok[++i]); g.movetime_set = true; }
            catch (const std::exception&) {}
        } else if (tok[i] == "wtime" && i + 1 < tok.size()) {
            try { g.wtime = std::stoll(tok[++i]); } catch (const std::exception&) {}
        } else if (tok[i] == "btime" && i + 1 < tok.size()) {
            try { g.btime = std::stoll(tok[++i]); } catch (const std::exception&) {}
        } else if (tok[i] == "winc" && i + 1 < tok.size()) {
            try { g.winc = std::stoll(tok[++i]); } catch (const std::exception&) {}
        } else if (tok[i] == "binc" && i + 1 < tok.size()) {
            try { g.binc = std::stoll(tok[++i]); } catch (const std::exception&) {}
        } else if (tok[i] == "movestogo" && i + 1 < tok.size()) {
            try { g.movestogo = std::stoi(tok[++i]); } catch (const std::exception&) {}
        } else if (tok[i] == "nodes" && i + 1 < tok.size()) {
            try { g.nodes_limit = std::stoull(tok[++i]); } catch (const std::exception&) {}
        } else if (tok[i] == "infinite") {
            g.infinite = true;
        } else if (tok[i] == "ponder") {
            g.ponder = true;
        } else if (tok[i] == "searchmoves") {
            ++i;
            while (i < tok.size()) {
                Move m = uci_to_move(pos, tok[i]);
                if (m == MOVE_NONE) break;
                g.search_moves.push_back(m);
                ++i;
            }
            --i; // compensate the outer loop's ++i
        }
    }
    return g;
}

std::string format_score(int score) {
    if (score >= MATE - MAX_DEPTH) {
        int moves = (MATE - score + 1) / 2;
        return "mate " + std::to_string(moves);
    }
    if (score <= -(MATE - MAX_DEPTH)) {
        int moves = (MATE + score + 1) / 2;
        return "mate -" + std::to_string(moves);
    }
    return "cp " + std::to_string(score);
}

std::string format_info_line(const IterationInfo& info, const std::vector<Move>& pv, int hashfull) {
    double seconds = info.time_ms / 1000.0;
    std::uint64_t nps = seconds > 0.0
        ? static_cast<std::uint64_t>(static_cast<double>(info.nodes) / seconds)
        : info.nodes;

    std::string s = "info depth " + std::to_string(info.depth) +
                     " seldepth " + std::to_string(info.seldepth) +
                     " multipv " + std::to_string(info.multipv_index + 1) +
                     " score " + format_score(info.score) +
                     " nodes " + std::to_string(info.nodes) +
                     " nps " + std::to_string(nps) +
                     " hashfull " + std::to_string(hashfull) +
                     " time " + std::to_string(info.time_ms) +
                     " pv";
    for (Move m : pv) s += ' ' + move_to_uci(m);
    return s;
}

long long compute_move_time(Color us, long long wtime, long long btime,
                            long long winc, long long binc, int movestogo) {
    long long t   = (us == WHITE) ? wtime : btime;
    long long inc = (us == WHITE) ? winc  : binc;
    if (t <= 0) return 1;
    int mtg = movestogo > 0 ? movestogo : 30;
    long long alloc = t / mtg + inc * 3 / 4;
    long long cap = t * 4 / 5;   // never spend more than 80% of the clock
    if (alloc > cap) alloc = cap;
    alloc -= 30;                 // communication / scheduling overhead
    if (alloc < 1) alloc = 1;
    return alloc;
}

void uci_loop() {
    Position pos;
    pos.set(kStartFen);
    std::vector<zobrist::Key> game_history{pos.key()};

    // Owned here, not inside search_best_move, so its contents (and the
    // allocation itself) persist across moves within the same game instead
    // of being rebuilt from scratch on every "go".
    TranspositionTable tt(16);
    int multipv_setting = 1;
    bool debug_mode = false;
    (void)debug_mode;
    // Set inside on_iteration (below) once at least one completed depth has
    // been reported for the current "go". If the search aborts before depth
    // 1 finishes (e.g. "go movetime 1", a tiny node limit, or an external
    // "stop" landing within depth 1), on_iteration never fires - without
    // this, only "bestmove" would be printed, and a tournament manager
    // (fastchess) expects at least one "info" line to know what the engine
    // thought of the position. Declared here (not inside the "go" branch)
    // because search_thread's lambda - which reads it after the branch's
    // own scope has exited - captures it by reference, so it must outlive
    // the branch.
    bool info_printed = false;

    // The search runs on its own thread so "stop" (read here on the main
    // thread) can actually interrupt an in-progress "go", instead of the
    // engine being stuck until search_best_move() returns on its own.
    std::thread search_thread;
    std::atomic<bool> stop_flag{false};
    auto join_search = [&]() {
        if (search_thread.joinable()) search_thread.join();
    };

    // Pondering: "go ponder" starts an unbounded search (like "go
    // infinite") on the position the GUI predicts will be reached, before
    // the opponent has actually moved. "ponderhit" means the prediction was
    // right and the GUI's clock params (stashed here from that "go ponder")
    // now apply for real; this engine converts the still-running unbounded
    // search into a timed one by starting a timer thread that sets
    // stop_flag once the newly-computed budget elapses, rather than
    // threading a mutable deadline into search_best_move.
    bool pondering = false;
    long long ponder_wtime = 0, ponder_btime = 0, ponder_winc = 0, ponder_binc = 0;
    int ponder_movestogo = 0;
    // Stashed on the main thread in the "go ponder" branch below, before
    // search_thread starts mutating `pos` concurrently. "ponderhit" must use
    // this instead of reading pos.side_to_move() live, since at that point
    // the ponder search thread is still running (join_search() is
    // deliberately skipped for "ponderhit") and pos is being mutated
    // in-place by do_move/undo_move on every search node - reading it from
    // the main thread there would be a data race.
    Color ponder_side = WHITE;
    std::thread ponder_timer;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::vector<std::string> tok{std::istream_iterator<std::string>(iss),
                                      std::istream_iterator<std::string>()};
        if (tok.empty()) continue;
        const std::string& cmd = tok[0];

        // Any command other than "go" needs the previous search fully
        // finished (and its bestmove printed) before touching `pos`, since
        // the search thread mutates it in place via do_move/undo_move.
        // "ponderhit" is the one exception: it must NOT join the still-
        // running ponder search - it converts it into a timed search in
        // place instead (see the ponder_timer comment above).
        if (cmd == "stop" || cmd == "quit") stop_flag = true;
        if (cmd != "ponderhit") join_search();
        if (cmd == "stop") {
            if (ponder_timer.joinable()) ponder_timer.join();
            continue;
        }

        if (cmd == "uci") {
            std::cout << "id name ChessEngine" << std::endl;
            std::cout << "id author ChessEngine Project" << std::endl;
            for (const std::string& opt_line : option_lines()) std::cout << opt_line << std::endl;
            std::cout << "uciok" << std::endl;
        } else if (cmd == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (cmd == "ucinewgame") {
            pos.set(kStartFen);
            game_history = {pos.key()};
            tt.clear();
        } else if (cmd == "debug") {
            debug_mode = parse_debug_command(tok, debug_mode);
        } else if (cmd == "ponderhit") {
            if (pondering) {
                pondering = false;
                long long budget = compute_move_time(ponder_side, ponder_wtime, ponder_btime,
                                                      ponder_winc, ponder_binc, ponder_movestogo);
                if (ponder_timer.joinable()) ponder_timer.join();
                ponder_timer = std::thread([&stop_flag, budget]() {
                    // Poll in short increments against a fixed deadline
                    // instead of one flat sleep_for(budget): if something
                    // else (a "stop" command's own `stop_flag = true`, or
                    // the search finishing on its own) already sets
                    // stop_flag first, this loop notices promptly and
                    // returns, so "stop"'s `ponder_timer.join()` doesn't
                    // block for the full budget. Otherwise it sets
                    // stop_flag itself once its own budget elapses,
                    // converting the ponder search into a timed one. The
                    // deadline is computed once from steady_clock rather
                    // than accumulated from nominal per-iteration sleep
                    // lengths, since actual sleep_for(10ms) calls can
                    // overshoot by the OS's timer-resolution granularity
                    // (e.g. ~15ms on Windows), which would otherwise
                    // compound over many iterations into a badly inflated
                    // total budget.
                    constexpr auto kPoll = std::chrono::milliseconds(10);
                    auto deadline = std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(budget);
                    while (!stop_flag.load() && std::chrono::steady_clock::now() < deadline) {
                        std::this_thread::sleep_for(kPoll);
                    }
                    if (!stop_flag.load()) stop_flag = true;
                });
            }
        } else if (cmd == "position") {
            size_t i = 1;
            std::string fen;
            if (i < tok.size() && tok[i] == "startpos") {
                fen = kStartFen;
                ++i;
            } else if (i < tok.size() && tok[i] == "fen") {
                ++i;
                std::vector<std::string> fields;
                while (i < tok.size() && tok[i] != "moves" && fields.size() < 6) {
                    fields.push_back(tok[i]);
                    ++i;
                }
                for (size_t k = 0; k < fields.size(); ++k) {
                    if (k) fen += ' ';
                    fen += fields[k];
                }
            }
            if (fen.empty()) continue; // malformed "position" command, ignore
            if (!has_one_king_per_side(fen)) continue; // kingless/invalid FEN, ignore
            if (!pos.set(fen)) continue; // malformed FEN, ignore; keep previous position

            std::vector<std::string> uci_moves;
            if (i < tok.size() && tok[i] == "moves") {
                uci_moves.assign(tok.begin() + i + 1, tok.end());
            }
            game_history = build_game_history(pos, uci_moves);
        } else if (cmd == "go") {
            GoOptions g = parse_go(pos, tok);
            SearchLimits lim;
            lim.depth = g.depth;
            lim.movetime_ms = g.movetime_ms;
            lim.nodes_limit = g.nodes_limit;
            lim.search_moves = g.search_moves;
            lim.multi_pv = multipv_setting;

            // Derive a per-move budget from the clock unless movetime is explicit.
            if (!g.movetime_set && !g.infinite && (g.wtime > 0 || g.btime > 0)) {
                lim.movetime_ms = compute_move_time(pos.side_to_move(), g.wtime, g.btime,
                                                    g.winc, g.binc, g.movestogo);
            }
            // When time or nodes governs the search, let iterative deepening
            // run deep and stop on that limit instead of the default fixed
            // depth.
            if (!g.depth_set && (lim.movetime_ms > 0 || lim.nodes_limit > 0)) {
                lim.depth = MAX_DEPTH;
            }
            // "go infinite"/"go ponder": search until "stop"/"ponderhit",
            // ignoring any clock/depth budget derived above.
            if (g.infinite || g.ponder) {
                lim.depth = MAX_DEPTH;
                lim.movetime_ms = 0;
            }
            if (lim.depth > MAX_DEPTH) lim.depth = MAX_DEPTH;

            if (g.ponder) {
                pondering = true;
                ponder_side = pos.side_to_move();
                ponder_wtime = g.wtime; ponder_btime = g.btime;
                ponder_winc = g.winc; ponder_binc = g.binc;
                ponder_movestogo = g.movestogo;
            } else {
                pondering = false;
            }

            lim.history = game_history;
            stop_flag = false;
            info_printed = false;
            lim.stop = &stop_flag;
            // Reported per completed depth from inside search_best_move()
            // (running on search_thread), so a GUI sees progress instead of
            // one info line at the very end. Safe to touch `pos`/`tt` here:
            // the search thread only calls back between root moves, when
            // both are back at the search's root state.
            lim.on_iteration = [&pos, &tt, &info_printed, history = lim.history](const IterationInfo& info) {
                std::vector<Move> pv = extract_pv(pos, tt, info.best, info.depth, history);
                std::cout << format_info_line(info, pv, tt.hashfull()) << std::endl;
                info_printed = true;
            };
            search_thread = std::thread([&pos, &tt, &info_printed, lim]() {
                SearchResult r = search_best_move(pos, lim, tt);
                if (!info_printed) {
                    // The search aborted before any iteration completed -
                    // synthesize a single info line from the final result so
                    // at least one is always printed before "bestmove".
                    IterationInfo info;
                    info.depth = r.depth;
                    info.score = r.score;
                    info.nodes = r.nodes;
                    info.best = r.best;
                    std::vector<Move> pv;
                    if (r.best != MOVE_NONE) pv = extract_pv(pos, tt, r.best, 1, lim.history);
                    std::cout << format_info_line(info, pv, tt.hashfull()) << std::endl;
                }
                if (r.best == MOVE_NONE) {
                    std::cout << "bestmove (none)" << std::endl;
                } else {
                    std::cout << "bestmove " << move_to_uci(r.best) << std::endl;
                }
            });
        } else if (cmd == "setoption") {
            SetOption opt = parse_setoption(tok);
            if (opt.name == "Hash") {
                try {
                    long mb = std::stol(opt.value);
                    if (mb >= 1) tt.resize(static_cast<std::size_t>(std::min(mb, 1024L)));
                } catch (const std::exception&) {}
            } else if (opt.name == "MultiPV") {
                try {
                    int v = std::stoi(opt.value);
                    if (v >= 1) multipv_setting = std::min(v, 256);
                } catch (const std::exception&) {}
            }
            // "Ponder" and any other option name are accepted and ignored:
            // Ponder is purely a GUI capability declaration - this engine
            // only ponders when explicitly told via "go ponder".
        } else if (cmd == "quit") {
            break;
        }
        // Unrecognized commands are silently ignored, per UCI convention.
    }

    // EOF on stdin (no explicit "quit") could otherwise leave a "go
    // infinite"/"go ponder" search running past the end of this function.
    stop_flag = true;
    join_search();
    if (ponder_timer.joinable()) ponder_timer.join();
}

} // namespace chess
