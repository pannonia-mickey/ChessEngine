#include "uci.hpp"

#include <atomic>
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

    // The search runs on its own thread so "stop" (read here on the main
    // thread) can actually interrupt an in-progress "go", instead of the
    // engine being stuck until search_best_move() returns on its own.
    std::thread search_thread;
    std::atomic<bool> stop_flag{false};
    auto join_search = [&]() {
        if (search_thread.joinable()) search_thread.join();
    };

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
        if (cmd == "stop" || cmd == "quit") stop_flag = true;
        join_search();
        if (cmd == "stop") continue;

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
            SearchLimits lim;
            long long wtime = 0, btime = 0, winc = 0, binc = 0;
            int movestogo = 0;
            bool depth_set = false, movetime_set = false, infinite = false;
            for (size_t i = 1; i < tok.size(); ++i) {
                if (tok[i] == "depth" && i + 1 < tok.size()) {
                    try { lim.depth = std::stoi(tok[++i]); depth_set = true; }
                    catch (const std::exception&) {}
                } else if (tok[i] == "movetime" && i + 1 < tok.size()) {
                    try { lim.movetime_ms = std::stoll(tok[++i]); movetime_set = true; }
                    catch (const std::exception&) {}
                } else if (tok[i] == "wtime" && i + 1 < tok.size()) {
                    try { wtime = std::stoll(tok[++i]); } catch (const std::exception&) {}
                } else if (tok[i] == "btime" && i + 1 < tok.size()) {
                    try { btime = std::stoll(tok[++i]); } catch (const std::exception&) {}
                } else if (tok[i] == "winc" && i + 1 < tok.size()) {
                    try { winc = std::stoll(tok[++i]); } catch (const std::exception&) {}
                } else if (tok[i] == "binc" && i + 1 < tok.size()) {
                    try { binc = std::stoll(tok[++i]); } catch (const std::exception&) {}
                } else if (tok[i] == "movestogo" && i + 1 < tok.size()) {
                    try { movestogo = std::stoi(tok[++i]); } catch (const std::exception&) {}
                } else if (tok[i] == "infinite") {
                    infinite = true;
                }
            }
            // Derive a per-move budget from the clock unless movetime is explicit.
            if (!movetime_set && !infinite && (wtime > 0 || btime > 0)) {
                lim.movetime_ms = compute_move_time(pos.side_to_move(), wtime, btime,
                                                    winc, binc, movestogo);
            }
            // When time governs the search, let iterative deepening run deep
            // and stop on the clock instead of the default fixed depth.
            if (!depth_set && lim.movetime_ms > 0) {
                lim.depth = MAX_DEPTH;
            }
            // "go infinite": search until "stop", ignoring any clock/depth
            // budget derived above.
            if (infinite) {
                lim.depth = MAX_DEPTH;
                lim.movetime_ms = 0;
            }
            if (lim.depth > MAX_DEPTH) lim.depth = MAX_DEPTH;

            lim.history = game_history;
            stop_flag = false;
            lim.stop = &stop_flag;
            search_thread = std::thread([&pos, &tt, lim]() {
                SearchResult r = search_best_move(pos, lim, tt);
                // A GUI/tournament manager (e.g. fastchess) expects at least
                // one "info ... score ..." line before "bestmove" to know
                // what the engine thought of the position.
                std::cout << "info depth " << r.depth << " score " << format_score(r.score)
                          << " nodes " << r.nodes << std::endl;
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
                    if (mb >= 1) tt.resize(static_cast<std::size_t>(mb));
                } catch (const std::exception&) {}
            } else if (opt.name == "MultiPV") {
                try {
                    int v = std::stoi(opt.value);
                    if (v >= 1) multipv_setting = v;
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
    // infinite" search running past the end of this function.
    stop_flag = true;
    join_search();
}

} // namespace chess
