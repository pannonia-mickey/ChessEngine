# ChessEngine

A UCI chess engine written in modern C++23.

## Goals

- **Protocol:** communicates with GUIs and tools through the [Universal Chess Interface](https://www.wbec-ridderkerk.nl/html/UCIProtocol.html).
- **Board representation:** bitboards, with sliding-piece moves generated from magic bitboards whose magic numbers are computed at startup.
- **Validated strength:** every technique that is meant to gain strength is accepted only after an [SPRT](https://www.chessprogramming.org/Sequential_Probability_Ratio_Test) test (e.g. with [fastchess](https://github.com/Disservin/fastchess) or [cutechess-cli](https://github.com/cutechess/cutechess)).

## Status

Project skeleton. The UCI loop answers `uci`, `isready`, `ucinewgame`, `position` and `go` (with a placeholder `bestmove 0000`) and exits on `quit`. Unknown commands are ignored, as the protocol requires.

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
go
bestmove 0000
quit
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
