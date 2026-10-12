# Roadmap

Improvement ideas from a code review of `main` after reverse futility pruning (b4136d5), ranked by
expected strength gain against effort. The Elo figures are rough expectations from engines of
similar strength with a hand-written evaluation, not measurements: every change still goes through
SPRT (see [sprt.md](sprt.md)). Ideas tried and rejected are recorded in
[experiments.md](experiments.md).

| # | Idea | Expected Elo | Effort | Files |
|---|---|---|---|---|
| 1 | Late move pruning and futility pruning of quiet moves | +30–60 | small | `src/search.cpp` |
| 2 | "Improving" flag (tunes RFP, LMP and LMR) | +10–25 | small | `src/search.cpp` |
| 3 | SEE pruning in the main search (losing captures, quiet moves) | +10–25 | small | `src/search.cpp`, `src/see.cpp` |
| 4 | Internal iterative reduction | +5–15 | very small | `src/search.cpp` |
| 5 | Countermove and continuation history, history-based LMR | +30–60 | medium | `src/search.cpp` |
| 6 | Transposition table in quiescence, static eval in the table, buckets | +15–35 | medium | `src/tt.hpp`, `src/tt.cpp`, `src/search.cpp` |
| 7 | Time management by best-move stability and score drops | +10–30 | small–medium | `src/search.cpp` |
| 8 | Speed: capture-only generation in quiescence, one evaluation per node | +5–15 | medium | `src/movegen.cpp`, `src/search.cpp` |
| 9 | Evaluation: tempo, threats by pieces, safe checks, endgame scaling | +10–40 | medium | `src/evaluate.cpp`, `src/eval_params.hpp` |
| 10 | Singular extensions | +15–40 | medium–large | `src/search.cpp`, `src/tt.*` |
| 11 | Lazy SMP (multiple threads) | +50–100 on several cores | large | `src/search.cpp`, `src/tt.*`, `src/uci.cpp` |
| 12 | NNUE evaluation | +150–300 | very large | new module and training pipeline |

Items 5 and 6 are implemented and passed SPRT (+16.2 ± 8.1 and +51.8 ± 14.8 Elo), and so did
the first part of item 9 (+26.6 ± 10.9 Elo); see their sections below.

Check extensions were ranked first but lost Elo in both forms tested (#20); see
[experiments.md](experiments.md#check-extensions-20-closed).

## Details

### 1. Late move pruning and futility pruning

Every quiet move is searched today; late move reductions only make the search shallower.

- **Late move pruning:** at shallow depth (for example `depth <= 8`) in a non-PV node not in check,
  once about `3 + depth * depth` moves were searched (more when improving), the remaining quiet
  moves are skipped.
- **Futility pruning:** at shallow depth, when `static_eval + 100 + 100 * depth <= alpha`, quiet
  moves are skipped.

Both use the `static_eval` that `negamax` already computes for reverse futility pruning.

### 2. Improving

Keep the static evaluation per ply in `PlyData`, and set
`improving = static_eval > stack[ply - 2].static_eval`. The RFP margin, the LMP move count and the
LMR amount can then depend on it. Items 1 and 3 build on it.

### 3. SEE pruning in the main search

At shallow depth, skip captures with `!see_ge(pos, move, -100 * depth)` and quiet moves with
`!see_ge(pos, move, -30 * depth * depth)`. The move picker's cached SEE results can be reused.

### 4. Internal iterative reduction

Without a transposition table move at `depth >= 4`, search one ply shallower. Two lines, usually a
gain.

### 5. Richer move ordering

Quiet moves are ordered by killers and a butterfly history (`[color][from][to]`) only. Add:

- a **countermove** table: the move that refuted the previous move;
- **continuation history** `[previous piece][previous to][piece][to]`, one and two plies back;
- **capture history** next to MVV-LVA.

Then adjust the LMR amount by the history score (less reduction for well-scored moves).

**Status:** implemented. `MovePicker` and the tables moved to `src/movepick.*` and
`src/history.hpp`; the LMR amount shrinks by one ply per 8192 of combined quiet history. Bench
58516 → 60229.

SPRT against `main` at 86583b5 (bench 58516), STC 8+0.08, gainer bounds [0, 5], fastchess with
`UHO_Lichess_4852_v1.epd`, concurrency 3:

```text
Elo: 16.24 +/- 8.11, nElo: 23.20 +/- 11.56
LOS: 100.00 %, DrawRatio: 39.16 %, PairsRatio: 1.28
Games: 3468, Wins: 1099, Losses: 937, Draws: 1432, Points: 1815.0 (52.34 %)
Ptnml(0-2): [94, 368, 679, 468, 125], WL/DD Ratio: 1.28
LLR: 2.95 (100.3%) (-2.94, 2.94) [0.00, 5.00]
SPRT ([0.00, 5.00]) completed - H1 was accepted
```

### 6. Transposition table

- Quiescence search neither probes nor stores the table.
- The 16-byte slot stores no static evaluation, so every node evaluates again; an `int16_t` eval
  field saves calls and lets a table bound refine `static_eval`.
- One entry per index: three or four entries per cache line, replaced by depth and age, lose fewer
  useful entries in long searches.

**Status:** implemented. Quiescence search probes the table (cutoffs outside PV nodes only),
orders by its move and stores its results at depth 0; a depth-0 result never replaces a deeper one
of the current search. Each 16-byte entry stores the static evaluation, which `negamax` and
quiescence reuse and refine with the table's score where the bound allows (never with mate
scores). Buckets of four entries fill a 64-byte cache line; a new position replaces an empty entry
or the one with the lowest `depth - 8 * age`. Bench 60229 → 48013.

SPRT against `main` at 6dbd879 (bench 60229), STC 8+0.08, gainer bounds [0, 5], fastchess with
`UHO_Lichess_4852_v1.epd`, concurrency 3:

```text
Elo: 51.77 +/- 14.82, nElo: 74.61 +/- 21.03
LOS: 100.00 %, DrawRatio: 38.17 %, PairsRatio: 2.15
Games: 1048, Wins: 393, Losses: 238, Draws: 417, Points: 601.5 (57.40 %)
Ptnml(0-2): [18, 85, 200, 166, 55], WL/DD Ratio: 1.41
LLR: 2.97 (100.8%) (-2.94, 2.94) [0.00, 5.00]
SPRT ([0.00, 5.00]) completed - H1 was accepted
```

### 7. Time management

`plan_time` is static (`available / 30 + increment * 3 / 4`). Scale the soft limit down when the
best move stays the same over several iterations and up when it changes or the score drops, and do
not start an iteration past about half of the soft limit, since it would not finish.

### 8. Speed

- Quiescence generates every legal move and lets the picker drop the quiet ones; a generator for
  captures and promotions only is faster.
- `negamax` evaluates for its pruning decisions and the depth-0 child evaluates again in
  quiescence; item 6's stored evaluation helps here too.
- A pawn hash table for the pawn structure and pawn shield terms.

Speedups that change the tree go through a non-regression test (`--test simplification`).

### 9. Evaluation

New terms are cheap to add and tune (see [tuning.md](tuning.md)). Missing terms that usually help:
a tempo bonus, threats by minor and major pieces (only pawn threats exist), safe checks and a
non-linear king attack score, outposts, and endgame scaling (opposite-colored bishops, little
material without pawns).

**Status:** partly implemented: a tempo bonus, threats by minor pieces and by rooks, safe checks
per piece type and knight and bishop outposts, tuned on the zurichess set (error 0.06158 →
0.05942). Bench 48013 → 41871. SPRT against the transposition table commit 71da732 (bench
48013), STC 8+0.08, gainer bounds [0, 5]:

```text
Elo: 26.61 +/- 10.90, nElo: 35.61 +/- 14.52
LOS: 100.00 %, DrawRatio: 37.49 %, PairsRatio: 1.44
Games: 2198, Wins: 802, Losses: 634, Draws: 762, Points: 1183.0 (53.82 %)
Ptnml(0-2): [71, 211, 412, 289, 116], WL/DD Ratio: 2.15
LLR: 2.98 (101.1%) (-2.94, 2.94) [0.00, 5.00]
SPRT ([0.00, 5.00]) completed - H1 was accepted
```
 Still open: a non-linear king attack score and endgame
scaling, which the linear Texel tuner cannot fit as they are; they need a tuner extension or hand
tuning with SPSA.

### 10. Singular extensions

Extend the table move when a reduced search excluding it fails low by a margin. Needs an excluded
move in `negamax` and reliable table depths; worth it once items 1–6 are in.

### 11. Lazy SMP

There is no `Threads` option and the table is not thread-safe. Single-threaded SPRT shows nothing,
but real games on several cores gain a lot. Needs lock-free (XOR-checked or atomic) table entries,
one `Searcher` per thread and a shared stop.

### 12. NNUE

The largest long-term gain, but needs data generation and training. Worth it once search
improvements level off.

## Code quality

- `src/search.cpp` holds move ordering, history and the search in one file; moving `MovePicker` and
  the history tables to their own files before item 5 keeps it readable.
- With more pruning constants, exposing them as hidden UCI options would allow SPSA tuning.
