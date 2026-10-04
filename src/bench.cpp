#include "bench.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include "position.hpp"
#include "search.hpp"

namespace chess {

std::uint64_t BenchResult::nps() const {
    return nodes * 1000 / static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed.count(), 1));
}

BenchResult run_bench(std::ostream& out, int depth) {
    BenchResult result;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kBenchFens.size(); ++i) {
        auto pos = Position::from_fen(kBenchFens[i]);
        if (!pos.has_value()) {
            continue;  // Unreachable: the bench positions are verified by the unit tests.
        }
        SearchLimits limits;
        limits.depth = depth;
        const std::uint64_t nodes = search(*pos, limits).nodes;
        out << "Position " << (i + 1) << '/' << kBenchFens.size() << ": " << kBenchFens[i]
            << "\nNodes: " << nodes << '\n';
        result.nodes += nodes;
    }
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    out << "\n===========================\n"
        << "Total time (ms) : " << result.elapsed.count() << '\n'
        << "Nodes searched  : " << result.nodes << '\n'
        << "Nodes/second    : " << result.nps() << '\n'
        << result.nodes << " nodes " << result.nps() << " nps\n"
        << std::flush;
    return result;
}

}  // namespace chess
