#pragma once

#include <iosfwd>
#include <string_view>

#include "position.hpp"

namespace chess {

// Implements the engine side of the Universal Chess Interface protocol.
// Input and output streams are injected so the protocol can be unit tested.
class Uci {
public:
    Uci(std::istream& in, std::ostream& out);

    // Reads commands until "quit" or end of input.
    void loop();

    // Handles a single command line. Returns false when the engine should exit.
    bool handle_command(std::string_view line);

    // The position set by the last valid "position" command (moves already played).
    [[nodiscard]] const Position& position() const noexcept { return position_; }

private:
    void cmd_uci();
    void cmd_isready();
    void cmd_ucinewgame();
    void cmd_position(std::string_view args);
    void cmd_go(std::string_view args);
    void cmd_perft(int depth);
    void cmd_bench(std::string_view args);
    void cmd_display();

    std::istream& in_;
    std::ostream& out_;
    Position position_;
};

}  // namespace chess
