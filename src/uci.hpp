#pragma once

#include <chrono>
#include <iosfwd>
#include <mutex>
#include <string_view>
#include <thread>

#include "position.hpp"
#include "tt.hpp"

namespace chess {

// Implements the engine side of the Universal Chess Interface protocol.
// Input and output streams are injected so the protocol can be unit tested.
//
// "go" searches on a background thread, so "stop", "isready" and "quit" are answered while the
// engine thinks. Commands that change or read the engine state stop a running search first.
class Uci {
public:
    Uci(std::istream& in, std::ostream& out);
    ~Uci();

    Uci(const Uci&) = delete;
    Uci& operator=(const Uci&) = delete;
    Uci(Uci&&) = delete;
    Uci& operator=(Uci&&) = delete;

    // Reads commands until "quit" or end of input. At end of input a running search is allowed
    // to finish.
    void loop();

    // Handles a single command line. Returns false when the engine should exit.
    bool handle_command(std::string_view line);

    // The position set by the last valid "position" command (moves already played).
    [[nodiscard]] const Position& position() const noexcept { return position_; }

    // Blocks until the running search, if any, has printed its best move.
    void wait();

private:
    // Asks the running search to stop and waits until it has printed its best move.
    void stop_search();
    // Writes one line; safe to call from the search thread while the main thread writes too.
    void send(std::string_view line);

    void cmd_uci();
    void cmd_setoption(std::string_view args);
    void cmd_isready();
    void cmd_ucinewgame();
    void cmd_position(std::string_view args);
    void cmd_go(std::string_view args);
    void cmd_perft(int depth);
    void cmd_bench(std::string_view args);
    void cmd_display();

    std::istream& in_;
    std::ostream& out_;
    std::mutex out_mutex_;
    Position position_;
    std::chrono::milliseconds move_overhead_;
    TranspositionTable tt_;
    // Declared last so it is joined before the members the search thread uses are destroyed.
    std::jthread search_thread_;
};

}  // namespace chess
