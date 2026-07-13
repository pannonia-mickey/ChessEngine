# Reverse Futility Pruning (Static Null-Move Pruning)

## Motivation

The engine's search side is already fairly mature: iterative deepening,
alpha-beta, a transposition table, null-move pruning, late move
reductions, aspiration windows, and SEE-based quiescence. An earlier
attempt at interior-node PVS null-window probing (`5cb34cd`) was tried
and reverted the same day (`7f69f20`) — it cut node counts but didn't
survive scrutiny, so that specific direction is closed for now.

Reverse Futility Pruning (RFP, a.k.a. static null-move pruning) is the
standard next addition after null-move pruning and LMR: at shallow
depth, if the static evaluation already exceeds beta by a safety
margin, the position is almost certainly winning enough that searching
it further is a waste — the node is cut immediately without recursing.
It's mechanically distinct from both null-move pruning (no child search
is made at all, just a static-eval check) and the reverted PVS attempt
(no window-narrowing involved), so it doesn't retread that ground.

## Goals

- Add a static-eval-based pruning check near the top of `negamax`, cutting
  shallow-depth nodes whose position is already comfortably above beta.
- Keep it strictly opt-out safe: never fires in check, never fires near
  mate scores (mirroring the existing null-move pruning guard), and only
  applies at shallow remaining depth.

## Non-goals

- No PV/non-PV node distinction. This codebase's `negamax` doesn't track
  that flag anywhere (existing null-move pruning doesn't either), so RFP
  follows the same convention rather than introducing new state.
- No changes to quiescence search, move ordering, or the transposition
  table.
- No margin/depth auto-tuning — constants are hand-set to common
  literature defaults and adjusted only if SPRT rejects the first
  attempt.

## Architecture

In `src/search.cpp`, inside `negamax()`, a new block is added after the
TT probe and before the existing null-move pruning block:

```cpp
constexpr int RFP_MAX_DEPTH = 8;
constexpr int RFP_MARGIN = 120;

if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
    int eval = evaluate(pos);
    int margin = RFP_MARGIN * depth;
    if (eval - margin >= beta)
        return eval - margin;
}
```

Placement rationale: it must come after `checked` is computed (already
available before the null-move block) and after the TT early-return
checks (a TT hit should still take priority — it's free information,
whereas RFP is a heuristic guess). It's placed *before* null-move
pruning because it's cheaper (no child search) and, when it fires,
saves the cost of even trying a null-move probe.

Conditions, matching the existing null-move pruning guard style:
- `!checked` — a static eval taken while in check is unreliable (there
  may be no way to avoid losing material or worse).
- `depth <= RFP_MAX_DEPTH` — only shallow remaining depth; deeper nodes
  have more riding on them than a static-eval margin can safely cover.
- `beta < MATE_THRESHOLD` — near-mate scores make eval margins
  meaningless, same reasoning as the null-move pruning guard.

The cutoff is fail-soft (returns `eval - margin`, a genuine lower bound
on the position's value, rather than the coarser `beta`), consistent
with `quiescence`'s existing fail-soft standing-pat return.

## Testing strategy (TDD)

Added to `tests/test_search.cpp`, following its existing style (FEN
positions, `SearchLimits`, `search_best_move`). Each failing test is
written before the corresponding implementation piece:

- **Node count drops in a lopsided position**: a position with a large
  material or positional imbalance in favor of the side to move, searched
  at a fixed depth within `RFP_MAX_DEPTH`. Asserts `result.nodes` falls
  below today's (pre-RFP) baseline node count for that position/depth,
  mirroring the node-count-assertion pattern used for the (reverted) PVS
  attempt and for null-move pruning's own tests.
- **Never fires in check**: a position where the side to move is in
  check but has a large positional/material edge (so a naive static eval
  would clear beta by a wide margin), with a forced only-legal reply.
  Asserts `search_best_move` still returns that legal reply, not an
  empty/pruned result — checking that `checked` correctly disables RFP.
- **Tactical correctness preserved**: a known mate-in-2 or similar sharp
  position (reused pattern from existing search tests) searched at a
  depth where RFP is active. Asserts the correct mating/best move is
  still found, guarding against RFP pruning away a real threat hidden
  behind a temporarily-low static eval for the *opponent* (i.e. making
  sure the margin check is one-sided and doesn't affect the side that's
  actually worse).

## Validation

Per `CLAUDE.md`, this is a search-heuristic strength change, not a
bugfix, so it requires SPRT validation regardless of how clean the unit
tests are: `tools/sprt/run_sprt.sh HEAD` after committing the change on
top of current `master`. Only kept on "H1 accepted".

If the first attempt (`RFP_MARGIN = 120`, `RFP_MAX_DEPTH = 8`) is
rejected (H0 / wash), the fallback is to retune the two constants
(literature range: margin 80-200 cp/ply, max depth 6-9) and re-run SPRT
once more before concluding the feature isn't a net gain for this engine
and reverting, the same way the PVS attempt was abandoned after it
didn't hold up.

## Risk notes

- RFP and null-move pruning both rely on "this position looks safely
  good enough to skip verifying," so there's some redundancy risk — RFP
  might fire on exactly the nodes where null-move pruning would already
  have caught the cutoff, in which case it adds node-count savings but
  little additional strength. This is an accepted, known trade-off in
  the literature (the two techniques are usually used together) and
  will show up directly in the SPRT result either way.
- A too-aggressive margin risks pruning away real tactics in sharp
  positions where material is level but one side has a strong attack the
  static eval doesn't see. The "tactical correctness preserved" test and
  the depth cap (`RFP_MAX_DEPTH = 8`) are the main guards against this;
  SPRT is the final arbiter.
