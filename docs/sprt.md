# SPRT testing

Every change that is meant to make the engine stronger is merged only after a
[Sequential Probability Ratio Test](https://www.chessprogramming.org/Sequential_Probability_Ratio_Test)
shows that it does. Changes that are meant to keep strength (simplifications, refactors that touch
search or evaluation, speedups that change the tree) must pass a non-regression test instead.

The test plays games between a **dev** build (the change) and a **base** build (usually `main`)
until the statistics decide between two hypotheses:

- **H0:** the Elo difference is `elo0` (the change does not gain what it should).
- **H1:** the Elo difference is `elo1` (it does).

The test stops as soon as the log-likelihood ratio (LLR) crosses one of the bounds set by `alpha` and
`beta` (both 0.05, so ±2.94). "H1 accepted" means the change passed.

## Requirements

- Python 3.9 or newer (the script uses only the standard library).
- CMake and a C++23 compiler, as for a normal build. Ninja is used when installed.
- A match runner on `PATH`:
  - [fastchess](https://github.com/Disservin/fastchess) (preferred): `git clone https://github.com/Disservin/fastchess && make -C fastchess -j`, then put the `fastchess` binary on `PATH` or pass `--runner-path`.
  - [cutechess-cli](https://github.com/cutechess/cutechess) also works (`--runner cutechess`), but its SPRT uses logistic Elo, so its bounds are not comparable with fastchess's default normalized Elo.

## Running a test

```sh
tools/sprt.py                                    # HEAD vs main, gainer bounds, STC
tools/sprt.py --test simplification              # non-regression test
tools/sprt.py --tc ltc                           # long time control
tools/sprt.py --dev my-branch --base 1a2b3c4     # any two git refs
tools/sprt.py --dry-run                          # build and bench only, print the command
```

The script:

1. Resolves `--dev` (default `HEAD`) and `--base` (default `main`, falling back to `origin/main`) to
   commits and builds each one in a temporary git worktree as a Release build without tests. Builds
   are cached by commit hash in `.sprt/bin/`, so re-running against the same base is instant.
   Only committed code is tested: commit your change before starting.
   `--dev-bin` / `--base-bin` use prebuilt binaries instead.
2. Runs `bench` on both binaries and prints the node counts (see [bench](#bench)).
3. Downloads the opening book on first use (see [Opening book](#opening-book)).
4. Plays the match: game pairs with colors reversed on the same opening (`-repeat`), random opening
   order, crash recovery, and draw/resign adjudication once the engine reports scores.
5. Saves `command.txt`, `runner.log`, `games.pgn` and `summary.md` in
   `.sprt/results/<time>-<dev>-vs-<base>/` and prints the summary.

Pressing Ctrl+C stops the match early; the summary is still written, marked inconclusive.

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--test gainer\|simplification` | `gainer` | Bounds preset (below) |
| `--elo0`, `--elo1` | from `--test` | Override the bounds |
| `--alpha`, `--beta` | `0.05` | Error rates |
| `--model` | `normalized` | Elo model of the bounds (fastchess only) |
| `--tc` | `stc` | `stc`, `ltc`, or any `seconds+increment` |
| `--book` | UHO_Lichess_4852_v1.epd | Opening book, `.epd` or `.pgn` |
| `--concurrency` | CPU threads − 1 | Games in parallel |
| `--rounds` | `20000` | Maximum game pairs before giving up |
| `--engine-option NAME=VALUE` | none | UCI option for both engines, repeatable (e.g. `Hash=16` once the engine has it) |
| `--runner`, `--runner-path` | auto | Choose fastchess or cutechess-cli |

Run `tools/sprt.py --help` for the full list.

## Bounds

The bounds are in normalized Elo (nElo), which, unlike plain Elo, does not depend on the draw ratio
of the time control and book.

| Kind of change | `--test` | Bounds |
| --- | --- | --- |
| Gainer: new search or evaluation feature, parameter tune | `gainer` | `[0, 5]` |
| Simplification, refactor that changes the tree, cleanup that removes code | `simplification` | `[-5, 0]` |

While the engine is young and changes gain tens of Elo, `[0, 5]` resolves in a few hundred to a few
thousand games. As gains get smaller, tighten the gainer bounds (for example `--elo1 3`) rather than
accepting noise.

## Time controls

| Preset | Time control | Use |
| --- | --- | --- |
| `stc` | 8+0.08 | Every test starts here |
| `ltc` | 40+0.4 | Confirmation for changes that may not scale: search reductions, pruning margins, time management |

A change that needs an LTC confirmation must pass STC first. Threads are 1 and every other engine
option is left at its default unless set with `--engine-option`.

Do not run other heavy work on the machine during a test, and keep `--concurrency` below the number
of physical cores so the engines are not starved of time.

## Opening book

The default book is `UHO_Lichess_4852_v1.epd` from
[official-stockfish/books](https://github.com/official-stockfish/books). Its positions are slightly
unbalanced, which lowers the draw rate and makes tests finish sooner. It is downloaded to
`.sprt/books/` on first use; if the download fails, fetch the zip yourself and pass `--book`.

## Bench

`bench` is a fixed, deterministic workload. Its node count is the build's fingerprint: two builds
with the same count walk the same tree, so a pure refactor or speedup must keep it unchanged, and
any change to search behavior changes it.

```text
$ ./build/release/src/chessengine bench        # also works as a UCI command: "bench [depth]"
Position 1/12: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
Nodes: 927023
...
===========================
Total time (ms) : 3465
Nodes searched  : 9988433
Nodes/second    : 2882664
9988433 nodes 2882664 nps
```

Specification:

- `bench [depth]` runs a fixed-depth search of the twelve positions in `kBenchFens`
  (`src/bench.hpp`) to `depth` (default `kDefaultBenchDepth`, chosen so that a release build takes
  a few seconds). The node count is the total of every node the search visits, quiescence included.
- The node count depends only on the code: no time limits, no randomness, a fresh state for each
  position (once the engine has a hash table, a fresh, fixed-size one).
- The last line is `<nodes> nodes <nps> nps`, the format OpenBench and similar tools parse.
- `chessengine bench` on the command line runs the same command and exits.

Every commit that changes the bench node count ends its message with a `Bench: <nodes>` line, so any
build in the history can be checked against the commit it claims to be.

## Recording results in a pull request

The pull request template has an SPRT section. Paste the `summary.md` the script printed into it:

```markdown
### SPRT

| | |
| --- | --- |
| Result | **passed (H1 accepted)** |
| Test | gainer |
| Bounds | [0.00, 5.00] (normalized), alpha=0.05, beta=0.05 |
| Time control | 8+0.08 |
| Opening book | UHO_Lichess_4852_v1.epd |
| Dev | `1a2b3c4d5e`, bench 655446288 |
| Base | `9f8e7d6c5b`, bench 655446288 |
| Runner | fastchess, concurrency 7 |

    Results of dev-1a2b3c4d5e vs base-9f8e7d6c5b (8+0.08, ...):
    Elo: 12.31 +/- 7.80, nElo: 20.02 +/- 12.67
    ...
    LLR: 2.95 (100.3%) (-2.94, 2.94) [0.00, 5.00]
```

Rules:

- The tested dev commit is the PR's head commit, or differs from it only in ways that keep bench
  (comments, docs, tests). If later commits change bench, re-run the test.
- The base is the `main` commit the branch was tested against. If `main` gains strength before the
  PR merges, re-run against the new `main` when the change interacts with what was merged.
- Include every test run for the change, failed ones too, with STC before LTC.
- Changes with no effect on play (docs, tests, tooling, pure refactors with an unchanged bench) need
  no SPRT; state "No functional change, bench unchanged: <nodes>" instead.
