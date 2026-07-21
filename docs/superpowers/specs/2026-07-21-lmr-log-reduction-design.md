# LMR Log-Based Reduction Table

## Motivation

`negamax`'s late move reduction (LMR, `search.cpp:405-412`) currently reduces every
eligible quiet move by a **fixed 1 extra ply** (the normal child search is `depth - 1`;
the LMR probe is `depth - 2`), regardless of how deep the remaining search is or how
far down the ordered move list the move sits. The engine is otherwise search-mature
(TT, null-move pruning, RFP, futility pruning, aspiration windows, SEE-ordered
quiescence, killer/history move ordering), but this flat reduction is the crudest
remaining piece: it treats a move reduced at `depth = 3` the same as one reduced at
`depth = 20`, and treats the 5th move in the list the same as the 50th, even though
move ordering (TT move, SEE-ordered captures, killers, history) means later moves in
a longer list are progressively less likely to be best.

Standard practice (and the engine's own comment at `search.cpp:399-404`, which already
frames LMR as "least likely to be best, per move ordering") is to scale the reduction
by both remaining depth and move index via a log-based table: `r ≈ base + log(depth) *
log(move_index) / divisor`. This reduces conservatively at shallow depth / early move
index (recovering close to today's behavior) and increasingly aggressively as either
grows, converting the existing move-ordering quality into fewer nodes per depth and,
under a real clock, more effective depth per move.

> Note: Principal Variation Search (null-window probing on every non-first move) was
> already implemented (`5cb34cd`) and reverted after SPRT rejected it (`7f69f20`), so
> it is out of scope here and not a fallback if this change also washes.

## Goals

- Replace LMR's fixed reduction (`depth - 2`) with a table-driven reduction
  `r = lmr_reduction(depth, move_index)`, applied as `reduced = max(1, depth - 1 - r)`.
- Keep the reduction conservative at shallow depth / early move index, matching
  today's behavior there, and only diverge (more aggressive) as depth and/or move
  index grow.
- Keep the LMR probe → conditional full-depth re-search structure exactly as it is
  today (this is what keeps LMR correctness-preserving: a reduction can only cost
  extra nodes via the re-search, never a wrong final score).

## Non-goals

- No change to LMR's **eligibility conditions** (`depth >= 3`, `move_index >= 4`,
  quiet, non-promotion, non-checking, not the TT move). Only the *magnitude* of the
  reduction changes.
- No killer/history-aware reduction adjustment, no "improving" flag, no PV/non-PV
  node distinction. These are independently valuable and independently SPRT-able;
  bundling them here would make a wash result impossible to attribute to a single
  cause. Left for future specs.
- No change to `quiescence` (it has no depth parameter to reduce) or to the root move
  loop in `search_best_move` (root moves are never LMR-reduced today and this spec
  doesn't change that).
- No new tuning infrastructure (e.g. runtime-configurable UCI options for the
  base/divisor constants). The two constants are hand-set to literature-typical
  starting points and adjusted only if SPRT rejects the first attempt, per this
  repo's existing pattern for RFP/futility margins.

## Architecture

All changes confined to `src/search.cpp`.

### 1. Reduction table (new helper in the anonymous namespace)

A `[MAX_DEPTH + 1][256]` table of `std::uint8_t` reductions, built once via a
function-local `static const` lazy initializer (thread-safe by the standard, and the
engine's search itself is single-threaded, so this costs nothing beyond the one-time
build):

```cpp
// Log-based LMR reduction: scales with both remaining depth and how far down
// the ordered move list a move sits, cutting harder the deeper and later a
// move is (later moves are progressively less likely to be best, per move
// ordering - see the LMR eligibility comment below). At depth=3, move_index=4
// (LMR's own minimum eligibility point) this evaluates to r=1, i.e. exactly
// today's flat depth-2 behavior; it only diverges (larger r) as depth and/or
// move_index grow past that point. Base (0.75) and divisor (2.25) are
// literature-typical starting constants, not derived from this engine's own
// data; see the design spec for the retuning fallback if SPRT rejects them.
int lmr_reduction(int depth, int move_index) {
    static const auto table = [] {
        std::array<std::array<std::uint8_t, 256>, MAX_DEPTH + 1> t{};
        for (int d = 1; d <= MAX_DEPTH; ++d)
            for (int m = 1; m < 256; ++m) {
                int r = static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.25);
                t[d][m] = static_cast<std::uint8_t>(std::max(1, r));
            }
        return t;
    }();
    return table[std::min(depth, MAX_DEPTH)][std::min(move_index, 255)];
}
```

Requires `#include <array>` and `#include <cmath>` (neither currently included in
`search.cpp`).

### 2. Applying it in the LMR branch (`search.cpp:405-412`)

The `do_lmr` eligibility condition is untouched. Only the reduced depth changes from
the flat `depth - 2` to a table lookup with a floor of 1:

```cpp
bool do_lmr = depth >= 3 && move_index >= 4 && !capture &&
              mf != PROMOTION && !gives_check && m != tt_move;
if (do_lmr) {
    int r = lmr_reduction(depth, move_index);
    int reduced = std::max(1, depth - 1 - r);
    score = -negamax(pos, reduced, -alpha - 1, -alpha, ply + 1,
                      nodes, tg, tt, tables, history);
    if (score > alpha)
        score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                          nodes, tg, tt, tables, history);
} else {
    score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tg, tt, tables, history);
}
```

Key points:

- **Floor of 1** (`std::max(1, depth - 1 - r)`): the reduced child search never drops
  to depth 0, which would send it straight into `quiescence` unconditionally from the
  LMR probe alone — same safety property the current flat `depth - 2` already has
  (guaranteed `>= 1` since `do_lmr` requires `depth >= 3`), just re-derived for a
  variable `r`.
- **Re-search condition unchanged** (`score > alpha`): a reduction can only ever
  produce extra nodes (via the re-search), never a wrong final score. This is what
  makes an arbitrarily larger `r` still safe to try — the worst case of an
  over-aggressive reduction is "re-search fires more often than ideal," not "wrong
  move reported."
- **Everything downstream is untouched**: best-move tracking, alpha update, beta
  cutoff, killer/history updates, TT store.

## Testing strategy (TDD)

Since this spec deliberately changes the search tree shape (unlike the minimal-scope
futility pruning spec, which pinned exact node-count thresholds), a fixed node-count
assertion is not the right correctness signal here — the whole point is that node
counts *change*, and by how much is exactly what SPRT (not a unit test) is meant to
judge. Tests instead assert **decision correctness is preserved**:

- **Existing suite as the primary safety net**: `tests/test_search.cpp`'s mate-in-N
  tests (`"finds mate in one"`, `"TT does not corrupt a forced mate score..."`,
  `"reverse futility pruning does not prevent finding a forced mate"`, `"futility
  pruning does not prevent finding a forced mate"`) and tactical tests (`"captures the
  free queen"`, `"avoids a losing rook-for-pawn trade..."`, `"finds a knight fork..."`)
  all depend on the LMR probe/re-search chain being exactly right — a broken re-search
  condition (skipping a needed re-search) would corrupt one of these scores. These
  must stay green.
- **New regression test**: a position with a wide-enough branching factor and enough
  search depth that LMR reduces many moves at a variety of move indices and depths
  (`r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq -`, depth 6 — already
  used elsewhere in the suite), asserting the reported best move is unchanged from
  today's (pre-change) master value and the score stays within a small tolerance of
  it. Exact score equality is *not* the right bar here: LMR (unlike PVS) is a genuine
  heuristic — a reduced-depth result that doesn't beat alpha is trusted as-is, never
  re-searched — so changing the reduction magnitude can legitimately shift the exact
  backed-up score by a few centipawns even when the decision doesn't change. This
  directly exercises the new `lmr_reduction()` table at a range of inputs (move
  indices well past 4, depths well past 3) without pinning a node count or an
  unrealistically exact score.
- **No direct unit test of `lmr_reduction()` itself**: it stays a private helper in
  `search.cpp`'s anonymous namespace, matching how every other internal search
  constant/helper in this file (`mvv_lva_score`, `move_order_score`, the RFP/futility
  margins) is exercised only indirectly through `search_best_move`'s observable
  behavior, not unit-tested in isolation. Its two defining properties — "reduces to
  1 at `depth=3, move_index=4`" and "non-decreasing in both inputs" — are true by
  construction of the formula (`0.75 + log(d)*log(m)/2.25` is monotonic in both `d`
  and `m` for `d, m >= 1`, and evaluates to `~0.75 + small` at the minimum eligible
  inputs, floored to 1), so a direct test would mostly re-assert arithmetic rather
  than catch a real defect; the regression test above is what actually exercises it
  end-to-end.

## Validation

Per `CLAUDE.md`, this is a search-heuristic strength change (it changes which nodes
get visited and, at fixed depth, node counts), not a bugfix — SPRT-required regardless
of how clean the unit tests are: `tools/sprt/run_sprt.sh master <candidate>`. Kept only
on "H1 accepted"; reverted on wash or regression.

If the first attempt (`base = 0.75`, `divisor = 2.25`) is rejected, the fallback is a
single retune (e.g. divisor in the 2.0–2.5 range) and one more SPRT run before
concluding the change isn't a net gain and reverting, matching this repo's existing
fallback pattern for RFP/futility margins.

## Risk notes

- The floor (`max(1, ...)`) and the untouched re-search condition are what keep this
  change correctness-preserving even for large `r` values; the main risk is purely a
  **strength** one (over- or under-reducing), which is exactly what SPRT is for.
- A too-aggressive table (large `r` at only moderate depth/move_index) risks pruning
  away a move that's actually best more often than the flat 1-ply reduction did,
  since the re-search only recovers moves that beat `alpha` — a move reduced so far
  that its probe score looks bad even though the real move is good would still be
  missed the same way today's flat reduction already accepts that risk, just with a
  larger effective reduction in some cases. This is inherent to LMR itself, not new to
  this change, and SPRT is the final arbiter.
- Building the table via a function-local `static` lazy initializer costs a one-time
  `MAX_DEPTH * 256` (~16K) loop of `std::log` calls on first call, negligible against
  a real search's node count, and is not on any hot per-node path afterward (only the
  lookup is, which is an O(1) array index).
