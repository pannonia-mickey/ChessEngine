#include "uci.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <istream>
#include <new>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bench.hpp"
#include "movegen.hpp"
#include "perft.hpp"
#include "search.hpp"
#include "util.hpp"

namespace chess {
namespace {

constexpr std::string_view kWhitespace = " \t\r\n";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

// Splits off the first whitespace-delimited token; the remainder is trimmed.
std::pair<std::string_view, std::string_view> split_first(std::string_view text) {
    text = trim(text);
    const auto end = text.find_first_of(kWhitespace);
    if (end == std::string_view::npos) {
        return {text, {}};
    }
    return {text.substr(0, end), trim(text.substr(end))};
}

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    while (!(text = trim(text)).empty()) {
        auto [token, rest] = split_first(text);
        tokens.emplace_back(token);
        text = rest;
    }
    return tokens;
}

constexpr int kDefaultMoveOverhead = 30;
constexpr int kMaxMoveOverhead = 5000;

// Parses the arguments of "go" into search limits. Unknown or malformed fields are skipped.
SearchLimits parse_go(const std::vector<std::string>& tokens) {
    SearchLimits limits;
    const auto value_after = [&](std::size_t& i) -> std::optional<int> {
        if (i + 1 >= tokens.size()) {
            return std::nullopt;
        }
        return parse_int(tokens[++i]);
    };
    const auto set_ms = [](std::optional<int> value, auto& field) {
        if (value.has_value()) {
            field = std::chrono::milliseconds(*value);
        }
    };
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& key = tokens[i];
        if (key == "wtime") {
            set_ms(value_after(i), limits.time[White]);
        } else if (key == "btime") {
            set_ms(value_after(i), limits.time[Black]);
        } else if (key == "winc") {
            set_ms(value_after(i), limits.increment[White]);
        } else if (key == "binc") {
            set_ms(value_after(i), limits.increment[Black]);
        } else if (key == "movetime") {
            set_ms(value_after(i), limits.move_time);
        } else if (key == "movestogo") {
            limits.moves_to_go = value_after(i).value_or(0);
        } else if (key == "depth") {
            limits.depth = std::max(value_after(i).value_or(limits.depth), 1);
        } else if (key == "nodes") {
            limits.nodes = static_cast<std::uint64_t>(std::max(value_after(i).value_or(0), 0));
        } else if (key == "infinite") {
            limits.infinite = true;
        }
    }
    return limits;
}

std::string format_info(const SearchInfo& info) {
    std::ostringstream line;
    line << "info depth " << info.depth << " seldepth " << info.selective_depth << " score ";
    if (is_mate_score(info.score)) {
        line << "mate " << mate_in_moves(info.score);
    } else {
        line << "cp " << info.score;
    }
    if (info.lower_bound) {
        line << " lowerbound";
    }
    const auto ms = std::max<std::int64_t>(info.elapsed.count(), 1);
    line << " nodes " << info.nodes << " nps " << info.nodes * 1000 / static_cast<std::uint64_t>(ms)
         << " hashfull " << info.hashfull << " time " << info.elapsed.count();
    if (!info.pv.empty()) {
        line << " pv";
    }
    for (const Move move : info.pv) {
        line << ' ' << move.to_uci();
    }
    return line.str();
}

}  // namespace

Uci::Uci(std::istream& in, std::ostream& out)
    : in_(in), out_(out), move_overhead_(kDefaultMoveOverhead) {}

Uci::~Uci() {
    stop_search();
}

void Uci::loop() {
    std::string line;
    while (std::getline(in_, line)) {
        if (!handle_command(line)) {
            stop_search();
            return;
        }
    }
    wait();
}

void Uci::wait() {
    if (search_infinite_) {
        search_thread_.request_stop();
    }
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
}

void Uci::stop_search() {
    search_thread_.request_stop();
    wait();
}

void Uci::send(std::string_view line) {
    const std::scoped_lock lock(out_mutex_);
    out_ << line << '\n' << std::flush;
}

bool Uci::handle_command(std::string_view line) {
    const auto [command, args] = split_first(line);

    // Commands answered while searching; every other one waits for the search to stop.
    if (command == "isready") {
        cmd_isready();
        return true;
    }
    if (command == "stop" || command == "quit") {
        stop_search();
        return command != "quit";
    }
    if (command.empty() || command == "ponderhit") {
        return true;
    }
    stop_search();

    if (command == "uci") {
        cmd_uci();
    } else if (command == "setoption") {
        cmd_setoption(args);
    } else if (command == "ucinewgame") {
        cmd_ucinewgame();
    } else if (command == "position") {
        cmd_position(args);
    } else if (command == "go") {
        cmd_go(args);
    } else if (command == "bench") {
        cmd_bench(args);
    } else if (command == "d") {
        cmd_display();
    }
    // The UCI specification requires unknown commands to be ignored.
    return true;
}

void Uci::cmd_uci() {
    send("id name ChessEngine " CHESS_ENGINE_VERSION);
    send("id author pannonia-mickey");
    send("option name Hash type spin default " +
         std::to_string(TranspositionTable::kDefaultSizeMb) + " min " +
         std::to_string(TranspositionTable::kMinSizeMb) + " max " +
         std::to_string(TranspositionTable::kMaxSizeMb));
    send("option name Move Overhead type spin default " + std::to_string(kDefaultMoveOverhead) +
         " min 0 max " + std::to_string(kMaxMoveOverhead));
    send("uciok");
}

void Uci::cmd_setoption(std::string_view args) {
    // setoption name <id> [value <x>], where the name may contain spaces.
    const auto tokens = tokenize(args);
    const auto name_it = std::ranges::find(tokens, "name");
    const auto value_it = std::ranges::find(tokens, "value");
    if (name_it == tokens.end() || value_it == tokens.end() || value_it + 1 == tokens.end()) {
        return;
    }
    std::string name;
    for (const auto& word : std::ranges::subrange(name_it + 1, value_it)) {
        name += (name.empty() ? "" : " ") + word;
    }
    const std::string& value = *(value_it + 1);

    if (name == "Hash") {
        const auto parsed = parse_int(value);
        if (parsed.has_value() && std::cmp_greater_equal(*parsed, TranspositionTable::kMinSizeMb) &&
            std::cmp_less_equal(*parsed, TranspositionTable::kMaxSizeMb)) {
            try {
                tt_.resize(static_cast<std::size_t>(*parsed));
            } catch (const std::bad_alloc&) {
                tt_.resize(TranspositionTable::kDefaultSizeMb);
                send("info string not enough memory for Hash " + value + ", using " +
                     std::to_string(TranspositionTable::kDefaultSizeMb));
            }
            return;
        }
    } else if (name == "Move Overhead") {
        const auto parsed = parse_int(value);
        if (parsed.has_value() && *parsed >= 0 && *parsed <= kMaxMoveOverhead) {
            move_overhead_ = std::chrono::milliseconds(*parsed);
            return;
        }
    }
    send("info string invalid option " + name + " value " + value);
}

void Uci::cmd_isready() {
    send("readyok");
}

void Uci::cmd_ucinewgame() {
    position_ = {};
    tt_.clear();
}

void Uci::cmd_position(std::string_view args) {
    const auto tokens = tokenize(args);
    if (tokens.empty()) {
        return;
    }

    const auto moves_it = std::ranges::find(tokens, "moves");
    std::optional<Position> position;

    if (tokens.front() == "fen") {
        std::string fen;
        for (const auto& field : std::ranges::subrange(tokens.begin() + 1, moves_it)) {
            if (!fen.empty()) {
                fen += ' ';
            }
            fen += field;
        }
        position = Position::from_fen(fen);
        if (!position) {
            out_ << "info string invalid fen\n" << std::flush;
            return;
        }
    } else if (tokens.front() == "startpos") {
        position.emplace();
    } else {
        return;
    }

    if (moves_it != tokens.end()) {
        for (const auto& text : std::ranges::subrange(moves_it + 1, tokens.end())) {
            const auto move = parse_uci_move(*position, text);
            if (!move) {
                out_ << "info string illegal move " << text << '\n' << std::flush;
                return;
            }
            position->make_move(*move);
        }
    }
    position_ = std::move(*position);
}

void Uci::cmd_go(std::string_view args) {
    const auto tokens = tokenize(args);
    if (tokens.size() >= 2 && tokens[0] == "perft") {
        const auto depth = parse_int(tokens[1]);
        if (depth.has_value() && *depth >= 1) {
            cmd_perft(*depth);
        }
        return;
    }

    SearchLimits limits = parse_go(tokens);
    limits.move_overhead = move_overhead_;
    search_infinite_ = limits.infinite;
    search_thread_ = std::jthread([this, limits, pos = position_](std::stop_token stop) mutable {
        const SearchResult result =
            search(pos, limits, tt_, std::move(stop),
                   [this](const SearchInfo& info) { send(format_info(info)); });
        // "0000" when there is no legal move.
        send("bestmove " + result.best_move.to_uci());
    });
}

void Uci::cmd_perft(int depth) {
    const auto start = std::chrono::steady_clock::now();
    auto entries = perft_divide(position_, depth);
    // Sorted output is easy to diff against other engines when hunting a move generation bug.
    std::ranges::sort(entries, {}, [](const PerftEntry& entry) { return entry.move.to_uci(); });
    std::uint64_t total = 0;
    for (const auto& [move, nodes] : entries) {
        out_ << move.to_uci() << ": " << nodes << '\n';
        total += nodes;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    const auto nps =
        total * 1000 / static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed.count(), 1));
    out_ << "\nNodes searched: " << total << '\n'
         << "info string time " << elapsed.count() << " ms, " << nps << " nps\n"
         << std::flush;
}

void Uci::cmd_bench(std::string_view args) {
    const auto tokens = tokenize(args);
    int depth = kDefaultBenchDepth;
    if (!tokens.empty()) {
        const auto parsed = parse_int(tokens.front());
        if (!parsed.has_value() || *parsed < 1) {
            out_ << "info string invalid bench depth " << tokens.front() << '\n' << std::flush;
            return;
        }
        depth = *parsed;
    }
    static_cast<void>(run_bench(out_, depth));
}

void Uci::cmd_display() {
    out_ << position_.pretty() << std::flush;
}

}  // namespace chess
