# Tuning the evaluation

The evaluation parameters in [`src/eval_values.hpp`](../src/eval_values.hpp) are fitted with
[Texel's tuning method](https://www.chessprogramming.org/Texel%27s_Tuning_Method): given many quiet
positions labeled with the result of the game they came from, find the parameters whose evaluation
best predicts those results.

## How it works

- Every term of the evaluation is a parameter (a middlegame and an endgame value) times how often its
  feature occurs for White minus how often for Black. The parameters live in one flat array; the
  layout (which index is which term) is in [`src/eval_params.hpp`](../src/eval_params.hpp).
- `trace_evaluation()` runs the evaluation in a mode that records those counts instead of adding up
  values. The tuner traces every position once, checks that the trace reproduces `evaluate()`
  exactly, and keeps a sparse list of (parameter, count) pairs per position.
- The predicted result of a position is `sigmoid(K * eval)` with the evaluation tapered by game
  phase. `K` is fitted first, so that the current parameters predict the results as well as they
  can; then all parameters are optimized together with Adam on the mean squared error over the whole
  data set.
- A parameter whose feature occurs in fewer than `--min-count` positions (default 2000) is not
  tuned: it would be fitted to the noise of those few games. Its value stays as it is in
  `eval_values.hpp`, so set such values by hand. The tuner lists them when it starts. In the current values the phalanx pawn bonuses on the sixth and seventh ranks and the two highest queen mobility entries are set this way.
- Every 100 epochs, and at the end, the tuner writes a complete `eval_values.hpp`.

The tuner is a separate program (`tools/tuner`), built with the other targets unless
`CHESS_BUILD_TOOLS=OFF`; the engine binary does not contain it.

## Data

The default data set is the zurichess `quiet-labeled.v7.epd` (1.43 million quiet positions from
engine games, each with the game result):

```sh
mkdir -p tuning && cd tuning
curl -LO https://bitbucket.org/zurichess/tuner/downloads/quiet-labeled.v7.epd.gz
gunzip quiet-labeled.v7.epd.gz
```

Any EPD or FEN file works when each line starts with a FEN (the move counters may be missing) and
contains the result as `1-0`, `0-1` or `1/2-1/2` (zurichess style), or `[1.0]`, `[0.5]` or `[0.0]`.
The positions should be quiet (no captures or checks pending), since the tuner evaluates them
statically.

## Running

```sh
cmake --workflow --preset release
./build/release/tools/tuner/tuner tuning/quiet-labeled.v7.epd --output src/eval_values.hpp
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--epochs N` | 1500 | Optimization steps over the whole data set |
| `--lr X` | 1.0 | Adam step size, in centipawns |
| `--threads N` | all hardware threads | Worker threads |
| `--limit N` | all | Use only the first N positions |
| `--min-count N` | 2000 | Parameters seen in fewer positions keep their value |
| `--k X` | fitted | Use this K instead of fitting it |
| `--output FILE` | `eval_values.hpp` | Where to write the parameters |

On the zurichess set the error has converged after about 500 epochs; the default 1500 take about a minute and a half on four cores.

## After tuning

A new set of values is a change to play: rebuild, update the bench node count in the commit message
and run an SPRT test against the previous values (see [sprt.md](sprt.md)) before merging.

To add a term: give it a place in `eval_params.hpp` (an offset, its size, and an entry in
`kParamGroups`), add zero entries for it in `eval_values.hpp`, compute its feature in
`src/evaluate.cpp` and score it with `add()`, then tune.
