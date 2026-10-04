#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace chess {

// The position most recently set by the "position" command, kept verbatim until a
// board representation exists to apply it to.
struct PositionCommand {
    std::string fen;                 // Empty means the standard start position.
    std::vector<std::string> moves;  // Moves in UCI long algebraic notation.
};

// Implements the engine side of the Universal Chess Interface protocol.
// Input and output streams are injected so the protocol can be unit tested.
class Uci {
public:
    Uci(std::istream& in, std::ostream& out);

    // Reads commands until "quit" or end of input.
    void loop();

    // Handles a single command line. Returns false when the engine should exit.
    bool handle_command(std::string_view line);

    [[nodiscard]] const PositionCommand& position() const noexcept { return position_; }

private:
    void cmd_uci();
    void cmd_isready();
    void cmd_ucinewgame();
    void cmd_position(std::string_view args);
    void cmd_go(std::string_view args);

    std::istream& in_;
    std::ostream& out_;
    PositionCommand position_;
};

}  // namespace chess
