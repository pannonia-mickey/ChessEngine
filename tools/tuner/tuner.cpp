// Texel tuner: fits the evaluation parameters to the results of the games a set of positions
// came from. See docs/tuning.md.
//
// The evaluation is linear in its parameters apart from the phase taper, so every position is
// traced once into a sparse list of (parameter, coefficient) pairs, and the error and its
// gradient are computed from those lists without running the evaluation again. The error is the
// mean squared difference between the game result and sigmoid(K * eval), where K is fitted first
// so the current parameters predict the results as well as they can. The parameters are then
// optimized with Adam on the full data set, and written out as a new eval_values.hpp.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "eval_params.hpp"
#include "eval_values.hpp"
#include "evaluate.hpp"
#include "magic.hpp"
#include "position.hpp"

namespace {

using chess::eval::kParamCount;
constexpr auto kParams = static_cast<std::size_t>(kParamCount);

struct Options {
    std::string dataset;
    std::string output = "eval_values.hpp";
    int epochs = 1500;
    double learning_rate = 1.0;
    unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    std::size_t limit = 0;
    std::size_t min_count = 2000;
    std::optional<double> k;
};

struct Feature {
    std::uint16_t index;
    std::int16_t coefficient;
};

// One position: its features are features[begin, end).
struct Entry {
    std::uint32_t begin;
    std::uint32_t end;
    double result;     // 1 White won, 0.5 draw, 0 Black won
    double mg_weight;  // phase / kMaxPhase
};

struct Dataset {
    std::vector<Entry> entries;
    std::vector<Feature> features;
};

struct Params {
    std::vector<double> mg = std::vector<double>(kParams);
    std::vector<double> eg = std::vector<double>(kParams);
};

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "tuner: " << message << '\n';
    std::exit(1);
}

void usage() {
    std::cerr << "usage: tuner <dataset.epd> [--epochs N] [--lr X] [--threads N] [--limit N]\n"
                 "             [--min-count N] [--k X] [--output FILE]\n";
    std::exit(2);
}

template <typename T>
T parse_number(std::string_view text) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        fail(std::format("not a number: {}", text));
    }
    return value;
}

Options parse_options(std::span<char*> args) {
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto value = [&]() -> std::string_view {
            if (i + 1 >= args.size()) {
                usage();
            }
            return args[++i];
        };
        if (arg == "--epochs") {
            options.epochs = parse_number<int>(value());
        } else if (arg == "--lr") {
            options.learning_rate = parse_number<double>(value());
        } else if (arg == "--threads") {
            options.threads = std::max(1U, parse_number<unsigned>(value()));
        } else if (arg == "--limit") {
            options.limit = parse_number<std::size_t>(value());
        } else if (arg == "--min-count") {
            options.min_count = parse_number<std::size_t>(value());
        } else if (arg == "--k") {
            options.k = parse_number<double>(value());
        } else if (arg == "--output") {
            options.output = value();
        } else if (arg.starts_with("--") || !options.dataset.empty()) {
            usage();
        } else {
            options.dataset = arg;
        }
    }
    if (options.dataset.empty()) {
        usage();
    }
    return options;
}

// The game result in a dataset line: "1-0", "0-1" or "1/2-1/2" (as in the zurichess sets), or
// [1.0], [0.5], [0.0].
std::optional<double> parse_result(std::string_view line) {
    if (line.find("1/2-1/2") != std::string_view::npos ||
        line.find("[0.5]") != std::string_view::npos) {
        return 0.5;
    }
    if (line.find("1-0") != std::string_view::npos ||
        line.find("[1.0]") != std::string_view::npos) {
        return 1.0;
    }
    if (line.find("0-1") != std::string_view::npos ||
        line.find("[0.0]") != std::string_view::npos) {
        return 0.0;
    }
    return std::nullopt;
}

// The evaluation from White's point of view with the compiled-in parameters, computed from a
// trace the way evaluate() computes it.
int traced_score(const chess::EvalTrace& trace) {
    int mg = 0;
    int eg = 0;
    for (std::size_t i = 0; i < kParams; ++i) {
        mg += trace.coefficients[i] * chess::eval::kParamValues[i].mg;
        eg += trace.coefficients[i] * chess::eval::kParamValues[i].eg;
    }
    return ((mg * trace.phase) + (eg * (chess::kMaxPhase - trace.phase))) / chess::kMaxPhase;
}

Dataset load(const Options& options) {
    std::ifstream in(options.dataset);
    if (!in) {
        fail(std::format("cannot open {}", options.dataset));
    }
    Dataset data;
    std::string line;
    std::size_t skipped = 0;
    while (std::getline(in, line) && (options.limit == 0 || data.entries.size() < options.limit)) {
        // FEN without the move counters: the first four fields, then the result.
        std::size_t end = 0;
        for (int field = 0; field < 4 && end != std::string::npos; ++field) {
            end = line.find(' ', end == 0 ? 0 : end + 1);
        }
        const std::string_view text(line);
        const auto result =
            end == std::string::npos ? std::nullopt : parse_result(text.substr(end));
        const auto pos = chess::Position::from_fen(text.substr(0, end));
        if (!result || !pos) {
            ++skipped;
            continue;
        }
        const chess::EvalTrace trace = chess::trace_evaluation(*pos);
        const int white_score =
            pos->side_to_move() == chess::White ? chess::evaluate(*pos) : -chess::evaluate(*pos);
        if (traced_score(trace) != white_score) {
            fail(std::format("the trace does not reproduce the evaluation of {}", line));
        }
        Entry entry{.begin = static_cast<std::uint32_t>(data.features.size()),
                    .end = 0,
                    .result = *result,
                    .mg_weight = static_cast<double>(trace.phase) / chess::kMaxPhase};
        for (std::size_t i = 0; i < kParams; ++i) {
            if (trace.coefficients[i] != 0) {
                data.features.push_back(
                    {.index = static_cast<std::uint16_t>(i),
                     .coefficient = static_cast<std::int16_t>(trace.coefficients[i])});
            }
        }
        entry.end = static_cast<std::uint32_t>(data.features.size());
        data.entries.push_back(entry);
    }
    if (skipped != 0) {
        std::cout << std::format("skipped {} lines without a FEN and a result\n", skipped);
    }
    if (data.entries.empty()) {
        fail("no positions loaded");
    }
    return data;
}

double evaluate(const Params& params, const Dataset& data, const Entry& entry) {
    double mg = 0;
    double eg = 0;
    for (std::uint32_t f = entry.begin; f < entry.end; ++f) {
        const Feature& feature = data.features[f];
        mg += feature.coefficient * params.mg[feature.index];
        eg += feature.coefficient * params.eg[feature.index];
    }
    return (mg * entry.mg_weight) + (eg * (1.0 - entry.mg_weight));
}

double sigmoid(double k, double score) {
    return 1.0 / (1.0 + std::exp(-k * score));
}

// Runs `work(first, last, thread)` over the entries split into one range per thread.
void parallel_for(const Options& options, std::size_t size,
                  const std::function<void(std::size_t, std::size_t, std::size_t)>& work) {
    std::vector<std::jthread> threads;
    const std::size_t chunk = (size + options.threads - 1) / options.threads;
    for (std::size_t t = 0; t < options.threads; ++t) {
        const std::size_t first = std::min(size, t * chunk);
        const std::size_t last = std::min(size, first + chunk);
        threads.emplace_back(work, first, last, t);
    }
}

double error(const Options& options, const Params& params, const Dataset& data, double k) {
    std::vector<double> sums(options.threads);
    parallel_for(
        options, data.entries.size(), [&](std::size_t first, std::size_t last, std::size_t t) {
            double sum = 0;
            for (std::size_t i = first; i < last; ++i) {
                const Entry& entry = data.entries[i];
                const double diff = entry.result - sigmoid(k, evaluate(params, data, entry));
                sum += diff * diff;
            }
            sums[t] = sum;
        });
    double total = 0;
    for (const double sum : sums) {
        total += sum;
    }
    return total / static_cast<double>(data.entries.size());
}

// The K that minimizes the error with the current parameters, by golden section search.
double fit_k(const Options& options, const Params& params, const Dataset& data) {
    const double ratio = (std::sqrt(5.0) - 1.0) / 2.0;
    double low = 0.0;
    double high = 0.05;
    double a = high - (ratio * (high - low));
    double b = low + (ratio * (high - low));
    double error_a = error(options, params, data, a);
    double error_b = error(options, params, data, b);
    while (high - low > 1e-7) {
        if (error_a < error_b) {
            high = b;
            b = a;
            error_b = error_a;
            a = high - (ratio * (high - low));
            error_a = error(options, params, data, a);
        } else {
            low = a;
            a = b;
            error_a = error_b;
            b = low + (ratio * (high - low));
            error_b = error(options, params, data, b);
        }
    }
    return (low + high) / 2.0;
}

// The gradient of the error with respect to every parameter, without the constant factor 2 / N
// (Adam does not need the scale).
Params gradient(const Options& options, const Params& params, const Dataset& data, double k) {
    std::vector<Params> partial(options.threads);
    parallel_for(options, data.entries.size(),
                 [&](std::size_t first, std::size_t last, std::size_t t) {
                     Params& g = partial[t];
                     for (std::size_t i = first; i < last; ++i) {
                         const Entry& entry = data.entries[i];
                         const double s = sigmoid(k, evaluate(params, data, entry));
                         const double d_score = (s - entry.result) * s * (1.0 - s) * k;
                         const double d_mg = d_score * entry.mg_weight;
                         const double d_eg = d_score * (1.0 - entry.mg_weight);
                         for (std::uint32_t f = entry.begin; f < entry.end; ++f) {
                             const Feature& feature = data.features[f];
                             g.mg[feature.index] += d_mg * feature.coefficient;
                             g.eg[feature.index] += d_eg * feature.coefficient;
                         }
                     }
                 });
    Params total;
    for (const Params& g : partial) {
        for (std::size_t i = 0; i < kParams; ++i) {
            total.mg[i] += g.mg[i];
            total.eg[i] += g.eg[i];
        }
    }
    return total;
}

void write_values(const Params& params, const std::string& path) {
    std::ostringstream out;
    out << "// Evaluation parameters, written by tools/tuner (see docs/tuning.md). The layout is "
           "in\n"
           "// eval_params.hpp.\n";
    out << "#pragma once\n\n#include <array>\n\n#include \"eval_params.hpp\"\n\n"
           "namespace chess::eval {\n\n// clang-format off\n"
           "inline constexpr std::array<PhaseScore, kParamCount> kParamValues = {{\n";
    for (const auto& group : chess::eval::kParamGroups) {
        out << "    // " << group.name << '\n';
        for (int row = 0; row < group.size; row += 8) {
            out << "   ";
            for (int i = row; i < std::min(group.size, row + 8); ++i) {
                const auto index = static_cast<std::size_t>(group.offset + i);
                out << std::format(" {{{:4},{:4}}},", std::lround(params.mg[index]),
                                   std::lround(params.eg[index]));
            }
            out << '\n';
        }
    }
    out << "}};\n// clang-format on\n\n}  // namespace chess::eval\n";
    std::ofstream file(path);
    file << out.str();
    if (!file) {
        fail(std::format("cannot write {}", path));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const Options options = parse_options(std::span(argv, static_cast<std::size_t>(argc)));
    static_cast<void>(chess::slider_tables());

    const auto start = std::chrono::steady_clock::now();
    const Dataset data = load(options);
    std::cout << std::format("loaded {} positions, {} features, {} parameters\n",
                             data.entries.size(), data.features.size(), kParams * 2);

    Params params;
    for (std::size_t i = 0; i < kParams; ++i) {
        params.mg[i] = chess::eval::kParamValues[i].mg;
        params.eg[i] = chess::eval::kParamValues[i].eg;
    }
    // A parameter seen in few positions would be fitted to their noise: keep its value.
    std::vector<std::size_t> counts(kParams);
    for (const Feature& feature : data.features) {
        ++counts[feature.index];
    }
    std::vector<bool> frozen(kParams);
    std::size_t frozen_count = 0;
    for (std::size_t i = 0; i < kParams; ++i) {
        frozen[i] = counts[i] < options.min_count;
        if (frozen[i]) {
            ++frozen_count;
        }
    }
    std::cout << std::format("{} parameters occur in fewer than {} positions and keep their value:",
                             frozen_count, options.min_count);
    for (const auto& group : chess::eval::kParamGroups) {
        for (int i = 0; i < group.size; ++i) {
            const auto index = static_cast<std::size_t>(group.offset + i);
            // Parameters that never occur (pawns on the back ranks, king material) are not news.
            if (frozen[index] && counts[index] != 0) {
                std::cout << std::format(" {}[{}]", group.name, i);
            }
        }
    }
    std::cout << '\n';

    const double k = options.k.value_or(fit_k(options, params, data));
    std::cout << std::format("K = {:.6f}, initial error {:.7f}\n", k,
                             error(options, params, data, k));

    // Adam.
    constexpr double kBeta1 = 0.9;
    constexpr double kBeta2 = 0.999;
    constexpr double kEpsilon = 1e-8;
    Params m;
    Params v;
    for (int epoch = 1; epoch <= options.epochs; ++epoch) {
        const Params g = gradient(options, params, data, k);
        const double correction1 = 1.0 - std::pow(kBeta1, epoch);
        const double correction2 = 1.0 - std::pow(kBeta2, epoch);
        const auto step = [&](std::vector<double>& p, std::vector<double>& m1,
                              std::vector<double>& m2, const std::vector<double>& grad) {
            for (std::size_t i = 0; i < kParams; ++i) {
                if (frozen[i]) {
                    continue;
                }
                m1[i] = (kBeta1 * m1[i]) + ((1.0 - kBeta1) * grad[i]);
                m2[i] = (kBeta2 * m2[i]) + ((1.0 - kBeta2) * grad[i] * grad[i]);
                p[i] -= options.learning_rate * (m1[i] / correction1) /
                        (std::sqrt(m2[i] / correction2) + kEpsilon);
            }
        };
        step(params.mg, m.mg, v.mg, g.mg);
        step(params.eg, m.eg, v.eg, g.eg);
        if (epoch % 100 == 0 || epoch == options.epochs) {
            const auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
            std::cout << std::format("epoch {:5}  error {:.7f}  {:.0f}s\n", epoch,
                                     error(options, params, data, k), elapsed.count());
            write_values(params, options.output);
        }
    }
    std::cout << std::format("wrote {}\n", options.output);
    return 0;
}
