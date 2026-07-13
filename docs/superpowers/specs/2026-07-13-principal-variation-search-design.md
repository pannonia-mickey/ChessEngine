# Principal Variation Search (PVS)

## Motivation

`negamax`'s move loop (`search.cpp:297-322`) searches every move with a
full `(-beta, -alpha)` window, except for the small subset of quiet
moves reduced by LMR (which get a null-window probe at reduced depth,
then a full-window re-search only if that probe beats alpha). Every
other move — including every move after the first at every node — pays
for a full-width search on the first try, even though move ordering
(TT move, SEE-ordered captures, killers, history) already puts the
likely-best move first at essentially every node.

PVS exploits that ordering: only the first move at a node gets a full
window. Every later move first gets a cheap null-window `(-alpha-1,
-alpha)` probe whose only job is to prove "not better than what we
already have." A null-window search is far cheaper than a full-window
one (fewer nodes fail to cut off), and the vast majority of later moves
genuinely aren't better, so they're refuted cheaply. Only on the rare
move that *does* beat alpha does the code pay for a full-window
re-search to get its exact value. Search is otherwise mature (TT,
null-move pruning, LMR, aspiration windows, SEE-ordered move ordering);
this is the largest remaining single-technique efficiency gap on the
search side, converting the existing move-ordering quality into fewer
nodes per depth — and, in real (time-controlled) play, effectively
deeper search in the same time.

Measured baseline: `r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R
w KQkq -`, depth 6 → **70,707 nodes** (current `master`, via UCI `go
depth 6`).

## Goals

- Add null-window PVS probing to `negamax`'s move loop for every move
  after the first, re-searching with the full window only when a probe
  beats alpha.
- Preserve LMR's existing reduced-depth null-window probe / full
  re-search chain, extending it (rather than replacing it) so reduced
  moves that beat alpha go through a full-depth null-window probe
  before ever paying for a full-window search.
- Measurably reduce node count at fixed depth on representative
  positions, without changing which move/score the search reports
  (PVS is a pure efficiency change, not a behavioral one).

## Non-goals

- No changes to the root move loop in `search_best_move`. It already
  narrows its window (`-beta, -a`) as `a` improves across root moves,
  which is standard alpha-beta window narrowing, not the null-window
  PVS trick — but the root's branching factor is small relative to the
  whole tree, and its window handling is already entangled with the
  aspiration-window retry loop. Extending PVS there is a separate,
  more surgical change and out of scope here.
- No changes to `quiescence` — it has no depth parameter to reduce and
  is not where PVS's node savings come from (its captures-only move
  list is already short and SEE-pruned).
- No new tuning constants; this is a search-shape change, not a new
  heuristic with weights to tune.

## Architecture

Change confined to `negamax`'s move loop in `search.cpp`. Current
structure:

```cpp
bool do_lmr = depth >= 3 && move_index >= 4 && !capture &&
              mf != PROMOTION && !gives_check && m != tt_move;
if (do_lmr) {
    score = -negamax(pos, depth - 2, -alpha - 1, -alpha, ...);
    if (score > alpha)
        score = -negamax(pos, depth - 1, -beta, -alpha, ...);
} else {
    score = -negamax(pos, depth - 1, -beta, -alpha, ...);
}
```

New structure:

```cpp
bool do_lmr = depth >= 3 && move_index >= 4 && !capture &&
              mf != PROMOTION && !gives_check && m != tt_move;
int move_search_depth = do_lmr ? depth - 2 : depth - 1;

if (move_index == 0) {
    score = -negamax(pos, depth - 1, -beta, -alpha, ...);
} else {
    score = -negamax(pos, move_search_depth, -alpha - 1, -alpha, ...);
    if (score > alpha && (do_lmr || score < beta))
        score = -negamax(pos, depth - 1, -beta, -alpha, ...);
}
```

Key points:

- **`move_index == 0`** (the first move — typically the TT move or top
  SEE-ordered capture): unchanged, full window, full depth. This is the
  presumed principal variation.
- **Every later move**: null-window probe first, at LMR's reduced depth
  when LMR's existing conditions apply, otherwise at full depth. This
  is the new behavior for the large class of moves that previously went
  straight to a full-window search.
- **Re-search condition** `score > alpha && (do_lmr || score < beta)`:
  - `score > alpha` — the probe wasn't refuted; it might actually be
    the new best move, so its exact value is needed.
  - For LMR moves (`do_lmr` true), always re-search when the probe
    beats alpha, matching today's behavior — the probe was at reduced
    depth, so even a probe result `>= beta` isn't trustworthy without a
    full-depth look.
  - For non-LMR moves, skip the re-search when the probe already
    reached `>= beta`: the null window is `(-alpha-1, -alpha)`, so a
    probe score `>= beta` (which is `> alpha`, since `beta > alpha`
    always holds here) already guarantees a beta cutoff at the parent
    regardless of the move's exact value — spending a full-window
    search just to learn a precise number that will be discarded by
    the cutoff is wasted work.
- Everything downstream of `score` (best-move tracking, alpha update,
  beta cutoff, killer/history updates, TT store) is untouched.

## Testing strategy (TDD)

1. **Red test first**, added to `tests/test_search.cpp`:

   ```cpp
   TEST_CASE("PVS null-window probing keeps node count well below plain alpha-beta") {
       attacks::init();
       Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
       SearchLimits lim; lim.depth = 6;
       TranspositionTable tt(16);
       SearchResult r = search_best_move(p, lim, tt);
       CHECK(r.nodes < 65000); // current master: 70,707 nodes at this depth
   }
   ```

   This fails against current `master` (70,707 ≥ 65,000) and is
   expected to pass once the move loop change lands. The 65,000 bound
   leaves ~8% margin below the measured baseline so the test isn't
   flaky against minor unrelated node-count noise, while still failing
   meaningfully before the change exists.

2. **Existing suite as the correctness safety net.** PVS is
   mathematically equivalent to full-window alpha-beta in the moves and
   scores it reports, provided the re-search-on-fail-high logic is
   correct — it is a pure search-shape optimization, not a new
   heuristic that changes which move is chosen. `tests/test_search.cpp`
   already exercises `negamax`'s internal move loop deeply (mate-in-one
   at depth 3, TT-persisted mate score at depth 5, warm-TT node
   reduction at depth 5, MultiPV at depth 2-3, repetition/fifty-move
   edge cases), all of which depend on the loop's fail-high/re-search
   behavior being exactly right. A broken re-search condition (e.g. one
   that skips re-searching a move that should have been re-searched)
   would corrupt a score and is likely to surface as one of these tests
   failing — the mate-score tests in particular are highly sensitive to
   any wrong value entering the TT or the best-move comparison. No new
   hand-built "forced re-search" position is added on top of this: the
   node-count test above is what specifically characterizes the new
   capability; correctness is what the existing suite already
   guarantees.

3. Implementation order: write the red node-count test, confirm it
   fails against current `master`, implement the move-loop change,
   confirm the new test passes and the full existing suite
   (`ctest --test-dir build --output-on-failure`) stays green.

## Validation

Per `CLAUDE.md`, this is a search heuristic change (not a bugfix), so
after all unit tests pass it must be validated with an SPRT run via
`tools/sprt/run_sprt.sh` against current `master`. The change is kept
only if SPRT reports "H1 accepted"; otherwise it is reverted regardless
of how clean the unit tests are — even though PVS is score-preserving
at a fixed search depth, its real playing-strength effect (faster
search → more effective depth under a real time control) is exactly
the kind of thing SPRT should confirm rather than assume.

## Risk notes

- The re-search condition is the one place a subtle bug could silently
  corrupt scores (skipping a needed re-search, or re-searching when
  unnecessary — the latter only costs nodes, not correctness). The
  testing strategy above leans on the existing deep/mate-sensitive
  tests specifically because they're likely to catch this class of bug
  even though they weren't written with PVS in mind.
- Combining PVS's null-window probing with LMR's existing reduced-depth
  probing in one conditional expression is slightly more intricate than
  either technique alone. This mirrors how the codebase already
  combines aspiration windows with the root loop's window narrowing, so
  it's consistent with the existing style rather than a new pattern.
