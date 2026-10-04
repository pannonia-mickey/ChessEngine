# ChessEngine

A UCI chess engine written in modern C++23.

## Goals

- **Protocol:** communicates with GUIs and tools through the [Universal Chess Interface](https://www.wbec-ridderkerk.nl/html/UCIProtocol.html).
- **Board representation:** bitboards, with sliding-piece moves generated from magic bitboards whose magic numbers are computed at startup.
- **Validated strength:** every technique that is meant to gain strength is accepted only after an [SPRT](https://www.chessprogramming.org/Sequential_Probability_Ratio_Test) test (e.g. with [fastchess](https://github.com/Disservin/fastchess) or [cutechess-cli](https://github.com/cutechess/cutechess)).

## Status

Board representation and fully legal move generation are in place; there is no search yet.

- Bitboards (one per piece type and color) plus a square-indexed mailbox.
- Sliding attacks from "fancy" magic bitboards. The magic numbers are not hardcoded: they are searched for at startup with a seeded PRNG (about 40 ms in a release build). Knight, king and pawn attacks and the Zobrist keys are generated at compile time.
- FEN parsing and output, make/unmake with an undo stack, castling, en passant, promotions, incremental Zobrist hashing.
- Legal move generation using check and pin masks; en passant is verified by testing the resulting position.
- `go perft <depth>` prints the node count per root move (sorted) and the total, verified against the standard [perft results](https://www.chessprogramming.org/Perft_Results).

The UCI loop answers `uci`, `isready`, `ucinewgame`, `position` and `go`, and exits on `quit`. Until search exists, `go` plays the first legal move. `d` prints the board, FEN and hash key. Unknown commands are ignored, as the protocol requires.

## Requirements

- CMake 3.25 or newer and Ninja
- A C++23 compiler: GCC 13+ or Clang 18+
- Optional: `clang-format` and `clang-tidy` (version 18)

Catch2 v3 is used for unit tests. An installed Catch2 3.x is used when found; otherwise it is fetched automatically at configure time.

## Building

The project uses [CMake presets](CMakePresets.json). Each workflow preset configures, builds and runs the tests:

```sh
cmake --workflow --preset release    # optimized build
cmake --workflow --preset debug      # debug build
cmake --workflow --preset sanitize   # debug build with AddressSanitizer + UBSan
```

Pick the compiler with the `CXX` environment variable, e.g. `CXX=clang++ cmake --workflow --preset debug`. Build output goes to `build/<preset>/`, and the engine binary is `build/<preset>/src/chessengine`.

Static analysis runs clang-tidy on every first-party source file during compilation:

```sh
cmake --preset tidy && cmake --build --preset tidy
```

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `CHESS_BUILD_TESTS` | `ON` | Build the unit tests |
| `CHESS_WARNINGS_AS_ERRORS` | `ON` | Treat compiler (and clang-tidy) warnings as errors |
| `CHESS_ENABLE_SANITIZERS` | `OFF` | Build with ASan + UBSan |
| `CHESS_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy while compiling |

## Usage

```text
$ ./build/release/src/chessengine
uci
id name ChessEngine 0.1.0
id author pannonia-mickey
uciok
isready
readyok
position startpos moves e2e4
go perft 1
a7a5: 1
a7a6: 1
...
g8h6: 1

Nodes searched: 20
info string time 0 ms, 20000 nps
quit
```

## Perft tests

The unit tests run perft on the six standard positions from the Chess Programming Wiki and on a suite of rule edge cases (illegal en passant, castling through check, promotions, stalemate, double check) at depths that keep debug builds fast. Deeper runs (up to 194 million leaves per position) are tagged hidden; run them on a release build:

```sh
./build/release/tests/chess_tests "[.deep]"
```

## Development

- Format code with `clang-format -i` before committing; CI checks formatting with clang-format 18.
- CI builds and tests with GCC 14 and Clang 18 in Debug, Release and sanitizer configurations, and runs clang-tidy.

## Project layout

```text
src/        engine sources (chess_core library + chessengine executable)
tests/      Catch2 unit tests
cmake/      CMake helper modules
```
