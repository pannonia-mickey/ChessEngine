#include "uci.hpp"
#include "attacks.hpp"
#include "zobrist.hpp"
#include "bench.hpp"

#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    chess::attacks::init();
    chess::zobrist::init();

    // "chess_engine bench [depth]" runs the NPS benchmark directly (useful
    // for scripts/CI) instead of entering the interactive UCI loop, which
    // otherwise blocks reading commands from stdin.
    if (argc > 1 && std::string(argv[1]) == "bench") {
        int depth = chess::BENCH_DEFAULT_DEPTH;
        if (argc > 2) depth = std::atoi(argv[2]);
        chess::run_bench(depth);
        return 0;
    }

    chess::uci_loop();
    return 0;
}
