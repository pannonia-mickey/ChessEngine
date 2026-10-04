#include "uci.hpp"

#include <algorithm>
#include <istream>
#include <ostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

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
    PositionCommand position;

    if (tokens.front() == "fen") {
        for (const auto& field : std::ranges::subrange(tokens.begin() + 1, moves_it)) {
            if (!position.fen.empty()) {
                position.fen += ' ';
            }
            position.fen += field;
        }
        if (position.fen.empty()) {
            return;
        }
    } else if (tokens.front() != "startpos") {
        return;
    }

    if (moves_it != tokens.end()) {
        position.moves.assign(moves_it + 1, tokens.end());
    }
    position_ = std::move(position);
}

void Uci::cmd_go(std::string_view /*args*/) {
    // Placeholder until move generation and search exist: "0000" is the UCI null move.
    out_ << "bestmove 0000\n" << std::flush;
}

}  // namespace chess
