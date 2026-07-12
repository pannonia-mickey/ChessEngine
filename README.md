# ChessEngine

A UCI-compatible chess engine written in modern C++23.

## Features

- **Board representation:** bitboards (LERF square indexing).
- **Move generation:** magic bitboards for sliding pieces (bishop/rook/queen), with magic numbers
  computed at runtime rather than loaded from a precomputed table; static attack tables for
  pawns, knights, and kings.
- **Search:** iterative-deepening negamax with alpha-beta pruning, quiescence search, a
  transposition table, null-move pruning, late move reductions (LMR), MultiPV, and
  abortable/time-/node-limited search (including `go ponder` / `ponderhit`).
- **Evaluation:** material balance plus piece-square tables (PST).
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

## Testing

Unit tests use [doctest](https://github.com/doctest/doctest) (fetched automatically by CMake):

```bash
ctest --test-dir build --output-on-failure
```

Move generation correctness is validated with perft tests. Changes affecting engine strength
(evaluation terms, weights, search heuristics) are validated with SPRT matches before being kept —
see `tools/sprt/README.md`.
