#include <cstddef>
#include <iostream>
#include <span>
#include <sstream>
#include <string>

#include "magic.hpp"
#include "uci.hpp"

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    // Search the magic numbers now rather than on the first move generation.
    static_cast<void>(chess::slider_tables());

    // Command line arguments are run as a single UCI command and the engine exits, so that
    // "chessengine bench" works the way testing frameworks such as OpenBench expect.
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() > 1) {
        std::string command;
        for (const char* arg : args.subspan(1)) {
            command += arg;
            command += ' ';
        }
        std::istringstream no_input;
        chess::Uci uci(no_input, std::cout);
        uci.handle_command(command);
        uci.wait();
        return 0;
    }

    chess::Uci uci(std::cin, std::cout);
    uci.loop();
    return 0;
}
