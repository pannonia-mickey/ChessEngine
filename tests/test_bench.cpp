#include "doctest.h"
#include "bench.hpp"
#include "attacks.hpp"
#include "zobrist.hpp"
#include <iostream>
#include <sstream>
#include <string>
using namespace chess;

namespace {
// Captures everything run_bench(depth) prints to stdout, so its node counts
// (embedded in the text) can be compared across runs without run_bench
// itself needing to return anything.
std::string capture_bench_output(int depth) {
    std::ostringstream captured;
    std::streambuf* old_buf = std::cout.rdbuf(captured.rdbuf());
    run_bench(depth);
    std::cout.rdbuf(old_buf);
    return captured.str();
}

// Extracts just the lines that carry node counts ("info string bench
// position ... nodes N" and "Nodes searched : N"), dropping the wall-clock
// "Total time"/"Nodes/second" lines - those are expected to vary run to run
// and aren't what determinism is being checked for here.
std::string node_count_lines(const std::string& output) {
    std::istringstream in(output);
    std::string line, result;
    while (std::getline(in, line)) {
        if (line.find("nodes") != std::string::npos ||
            line.find("Nodes searched") != std::string::npos) {
            result += line;
            result += '\n';
        }
    }
    return result;
}
} // namespace

TEST_CASE("bench is deterministic: same depth produces identical node counts") {
    attacks::init();
    zobrist::init();
    // Depth kept small so the test stays fast; determinism (not raw NPS) is
    // what's being verified here.
    std::string first = capture_bench_output(2);
    std::string second = capture_bench_output(2);
    CHECK(node_count_lines(first) == node_count_lines(second));
    CHECK(!node_count_lines(first).empty());
    CHECK(first.find("Nodes/second") != std::string::npos);
}
