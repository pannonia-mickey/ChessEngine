#include "uci.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "movegen.hpp"
#include "perft.hpp"
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

}  // namespace

Uci::Uci(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

void Uci::loop() {
    std::string line;
    while (std::getline(in_, line)) {
        if (!handle_command(line)) {
            break;
        }
    }
}

bool Uci::handle_command(std::string_view line) {
    const auto [command, args] = split_first(line);

    if (command == "uci") {
        cmd_uci();
    } else if (command == "isready") {
        cmd_isready();
    } else if (command == "ucinewgame") {
        cmd_ucinewgame();
    } else if (command == "position") {
        cmd_position(args);
    } else if (command == "go") {
        cmd_go(args);
    } else if (command == "d") {
        cmd_display();
    } else if (command == "quit") {
        return false;
    }
    // The UCI specification requires unknown commands (and "stop" while idle) to be ignored.
    return true;
}

void Uci::cmd_uci() {
    out_ << "id name ChessEngine " CHESS_ENGINE_VERSION "\n"
         << "id author pannonia-mickey\n"
         << "uciok\n"
         << std::flush;
}

void Uci::cmd_isready() {
    out_ << "readyok\n" << std::flush;
}

void Uci::cmd_ucinewgame() {
    position_ = {};
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

    // Placeholder until search exists: play the first legal move ("0000" when there is none).
    const MoveList moves = generate_legal_moves(position_);
    const Move best = moves.empty() ? Move::null() : moves[0];
    out_ << "bestmove " << best.to_uci() << '\n' << std::flush;
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

void Uci::cmd_display() {
    out_ << position_.pretty() << std::flush;
}

}  // namespace chess
