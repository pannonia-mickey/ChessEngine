# Futility Pruning

## Motivation

The main `negamax` move loop already has reverse futility pruning (RFP,
whole-node, compares static eval against *beta*) and late move
reductions (LMR, reduces rather than skips). Futility pruning is the
natural companion to RFP: instead of a whole-node cutoff, it's a
per-move skip inside the loop — at shallow remaining depth, if the
static eval plus a safety margin still can't clear *alpha*, a quiet
move at that node is extremely unlikely to be the best move, so it's
skipped without ever recursing into it. RFP and futility pruning are
almost always implemented together in the literature (they share the
same "static eval is a reliable-enough proxy at shallow depth"
assumption, just on opposite bounds), so this closes that pair.

## Goals

- Skip searching quiet, non-critical moves inside the `negamax` move
  loop when the static eval plus a depth-scaled margin already can't
  reach `alpha`, at shallow remaining depth.
- Reuse the RFP block's static eval computation instead of calling
  `evaluate(pos)` a second time per node.
- Keep it strictly opt-out safe: never skips the first move at a node,
  the TT move, a capture, a promotion, or a move that gives check.

## Non-goals

- No move-count/late-move pruning (skipping quiet moves purely by move
  index, independent of eval). That's a separate, independently
  SPRT-able technique and is left for a future spec.
- No PV/non-PV node distinction, matching RFP and null-move pruning's
  existing convention in this codebase.
- No static (pre-`do_move`) "does this move give check" predicate.
  Checking `gives_check` after `do_move` (as LMR already does) is
  simpler and reuses code the engine already pays for; building a cheap
  static check-predicate is extra scope this spec doesn't need.
- No margin auto-tuning — constants are hand-set to a literature-typical
  starting point and adjusted only if SPRT rejects the first attempt.

## Architecture

In `src/search.cpp`, inside `negamax()`, the existing RFP block's
`evaluate(pos)` call is hoisted so both RFP and the new futility check
share one `eval` computation per node instead of each computing its
own:

```cpp
bool checked = in_check(pos);

constexpr int RFP_MAX_DEPTH = 8;
constexpr int RFP_MARGIN = 120;
constexpr int FUTILITY_MAX_DEPTH = 8;
constexpr int FUTILITY_MARGIN = 150;

int eval = 0;
if (!checked && depth <= std::max(RFP_MAX_DEPTH, FUTILITY_MAX_DEPTH))
    eval = evaluate(pos);

if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
    int margin = RFP_MARGIN * depth;
    if (eval - margin >= beta)
        return eval - margin;
}

bool futility_possible = !checked && depth <= FUTILITY_MAX_DEPTH &&
                          alpha > -MATE_THRESHOLD;
int futility_score = eval + FUTILITY_MARGIN * depth;
```

`futility_possible`/`futility_score` are computed once before the move
loop starts (mirroring how `checked` and the RFP eval are already
computed once per node, not per move).

Inside the move loop, the skip is placed right after `gives_check` is
computed — the same point LMR already makes its own per-move decision
— and before the LMR/full-depth search branch:

```cpp
pos.do_move(m, st);
history.push_back(pos.key());
bool gives_check = in_check(pos);

if (futility_possible && move_index > 0 && m != tt_move && !capture &&
    mf != PROMOTION && !gives_check && futility_score <= alpha) {
    history.pop_back();
    pos.undo_move(m, st);
    continue;
}

int score;
bool do_lmr = ...
```

Conditions:
- `!checked` (via `futility_possible`) — same reasoning as RFP: a
  static eval taken while in check is unreliable.
- `depth <= FUTILITY_MAX_DEPTH` — shallow remaining depth only.
- `alpha > -MATE_THRESHOLD` — near-mate bounds make eval margins
  meaningless, same reasoning as RFP's `beta < MATE_THRESHOLD` guard,
  mirrored on the alpha side since futility compares against alpha.
- `move_index > 0` — the first move at a node is always searched in
  full; a node can never have every one of its moves pruned away.
- `m != tt_move` — never skip the transposition table's recommended
  move.
- `!capture && mf != PROMOTION && !gives_check` — only quiet,
  non-tactical moves are skipped; captures, promotions, and checks can
  swing eval by more than a static margin predicts.

Placement rationale for computing this after `do_move` rather than
before: knowing whether a move gives check cheaply, without making it,
would need a new static predicate that doesn't exist in this codebase.
LMR already accepts the cost of `do_move`/`undo_move` before deciding
whether to reduce; futility pruning follows the same precedent instead
of introducing new machinery to save that one make/unmake pair.

## Testing strategy (TDD)

Added to `tests/test_search.cpp`, following the existing RFP test
style (FEN positions, `SearchLimits`, `search_best_move`), each written
before the corresponding implementation piece:

- **Node count drops in a hopeless quiet position**: a position where
  the side to move is comfortably lost with no tactical resource,
  searched at a fixed depth within `FUTILITY_MAX_DEPTH`. Asserts
  `result.nodes` falls below today's (pre-futility) baseline node count
  for that position/depth.
- **Never fires in check**: a position where the side to move is in
  check with a forced-looking but non-unique reply. Asserts the search
  still considers all legal replies (via a node-count or best-move
  check), confirming `checked` disables the pruning as intended.
- **Never skips a capture, promotion, or checking move**: a position
  constructed so the *only* good move at a pruning-eligible node is a
  capture (or promotion, or check) that a naive quiet-move skip would
  otherwise never reach. Asserts `search_best_move` still finds it.
- **Tactical correctness preserved**: the existing mate-in-1 position
  (reused from "finds mate in one"), searched at a depth where futility
  pruning is active. Asserts the mate is still found.

## Validation

Per `CLAUDE.md`, this is a search-heuristic strength change, not a
bugfix, so it requires SPRT validation regardless of how clean the unit
tests are: `tools/sprt/run_sprt.sh HEAD` after committing the change on
top of current `master`. Only kept on "H1 accepted".

If the first attempt (`FUTILITY_MARGIN = 150`, `FUTILITY_MAX_DEPTH = 8`)
is rejected (H0 / wash), the fallback is to retune the margin
(literature range: roughly 100-300 cp/ply for this kind of per-move
futility check, wider than RFP's whole-node margin since it's applied
against alpha on every quiet move rather than once per node) and
re-run SPRT once more before concluding the feature isn't a net gain
for this engine and reverting.

## Risk notes

- Futility pruning and RFP both key off the same static eval and depth
  window, so there's some shared-assumption risk: if the eval is
  systematically wrong in a class of positions (e.g. a coming king
  attack the eval doesn't see), both techniques mis-fire together
  rather than one catching the other's mistake. This is a known,
  accepted trade-off in the literature — the two are near-universally
  paired anyway — and SPRT is the final arbiter.
- Sharing the `eval` computation between RFP and futility pruning means
  the `evaluate(pos)` guard condition changes from "RFP's own depth
  check" to "either RFP's or futility's depth check" — since both use
  `RFP_MAX_DEPTH == FUTILITY_MAX_DEPTH == 8` today, this is a no-op in
  practice, but the two constants are independent knobs and could
  diverge in future retuning; the `std::max` guard keeps that safe.
- As with RFP, a too-aggressive margin risks skipping real tactics in
  positions where material is level but a quiet move (e.g. a quiet
  threat-creating move, not a capture/check) is actually critical. The
  "never skips a capture/promotion/check" and "tactical correctness
  preserved" tests are the main guards; SPRT is the final arbiter.
