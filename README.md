# ChessEngine

A UCI-compatible chess engine written in modern C++23.

## Features

- **Board representation:** bitboards (LERF square indexing).
- **Move generation:** magic bitboards for sliding pieces (bishop/rook/queen), with magic numbers
  computed at runtime rather than loaded from a precomputed table; static attack tables for
  pawns, knights, and kings.
- **Search:** iterative-deepening negamax with alpha-beta pruning and aspiration windows,
  quiescence search (captures filtered/ordered by static exchange evaluation, SEE), a
  transposition table, null-move pruning, reverse futility pruning (RFP), late move
  reductions (LMR), killer/history move ordering, MultiPV, and abortable/time-/node-limited
  search (including `go ponder` / `ponderhit`).
- **Evaluation:** tapered (middlegame/endgame phase-interpolated) material balance plus
  piece-square tables (PST), mobility, bishop pair, rook open/semi-open file bonus, passed
  pawn bonus, and king safety (pawn shield).
- **Protocol:** [UCI](https://www.chessprogramming.org/UCI) (Universal Chess Interface) —
  `position`, `go` (depth/movetime/clock/nodes/searchmoves/infinite/ponder), `stop`, `ponderhit`,
  `setoption` (Hash, Ponder, MultiPV), `debug`, `ucinewgame`.

## Building

Requires a C++23 compiler and CMake 3.20+.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces the `chess_engine` UCI binary under `build/`, ready to be pointed to by any UCI
chess GUI (e.g. Arena, CuteChess, Fritz).

The `CHESS_NATIVE_ARCH` and `CHESS_LTO` CMake options (both default `ON`) enable extra
compile-time speed tuning. On GCC/Clang, `CHESS_NATIVE_ARCH` adds `-march=native`/`-mcpu=native`;
on MSVC it adds a fixed `/arch:AVX2` instead (MSVC has no host-detecting "native" flag).
`CHESS_LTO` enables link-time/interprocedural optimization on both toolchains (on MSVC,
`/GL` + `/LTCG`).

## Benchmarking

```bash
./build/chess_engine bench [depth]
```

Runs a fixed-position, fixed-depth search benchmark (also available as the `bench` UCI command)
and prints a Stockfish-style `Nodes searched` / `Total time` / `Nodes/second` summary. The node
count is a reproducible regression anchor: a speed-only change (build flags, data structures,
allocation reduction) must leave it bit-for-bit identical while only changing the elapsed
time/NPS. See `docs/perf/nps-baseline.md` for the current baseline and measurement protocol.

## Testing

Unit tests use [doctest](https://github.com/doctest/doctest) (fetched automatically by CMake):

```bash
ctest --test-dir build --output-on-failure
```

Move generation correctness is validated with perft tests. Changes affecting engine strength
(evaluation terms, weights, search heuristics) are validated with SPRT matches before being kept —
see `tools/sprt/README.md`. Speed-only changes are validated against the `bench` command instead —
see `docs/perf/nps-baseline.md`.
