# Pawn Structure Evaluation (Isolated / Doubled / Backward) + Pawn Hash

## Motivation

The evaluation currently accounts for material+PST (tapered), mobility,
bishop pair, rook open/semi-open file, passed pawns, and a simple king
pawn-shield — but nothing about the *quality* of a side's own pawn
structure beyond passed pawns. Isolated, doubled, and backward pawns are
well-established, high-signal weaknesses that most engines score, and
none of them are covered yet.

Pawn-only facts (which pawns are isolated/doubled/backward/passed) never
depend on any non-pawn piece, and never change unless a pawn moves,
captures, or gets captured — a much rarer event per node than "any move
happens." That makes this evaluation naturally cacheable by a
pawn-only Zobrist key, the same way the transposition table caches
whole-position search results, except a pawn-hash entry never goes
stale: a given pawn structure scores the same in any position, any
game, forever. This design folds the *existing* `passed_pawn_bonus()`
into the same cached computation rather than leaving a second,
uncached, redundant pawn scan next to the new one.

## Goals

- Add isolated, doubled, and backward pawn penalties (MG/EG tapered),
  computed alongside the existing passed-pawn bonus in one consolidated
  function.
- Cache that combined pawn-only score behind a pawn hash table, keyed by
  a new incrementally-maintained `Position::pawn_key()`.
- Keep `evaluate()`'s public signature unchanged (`int evaluate(const
  Position&)`) — the pawn hash table lives inside the new translation
  unit, not threaded through call sites.

## Non-goals

- No connected/phalanx pawn bonus, no pawn islands, no pawn chains —
  scoped out to keep this one SPRT-attributable change (see the
  brainstorming discussion that scoped this down from a larger set).
- No constant auto-tuning (SPSA or similar) — initial values are
  literature-typical estimates; SPRT decides keep/revert as-is, per
  `CLAUDE.md`'s tuning rule.
- No multi-threading support for the pawn hash table (e.g. per-thread
  instances). The engine's search is single-threaded today; if Lazy SMP
  is added later, the pawn hash table will need to move from a
  file-static instance to one owned per search thread.

## Architecture

### New files: `src/pawn_eval.hpp` / `src/pawn_eval.cpp`

```cpp
// pawn_eval.hpp
namespace chess {
// Isolated + doubled + backward penalties and the passed-pawn bonus,
// tapered MG/EG, White minus Black. Backed by a process-lifetime pawn
// hash cache keyed on pos.pawn_key() - never needs clearing, since a
// given pawn structure scores identically in any position/game.
void pawn_structure(const Position& pos, int& mg, int& eg);
}
```

`pawn_eval.cpp` holds:
- The moved-and-extended pawn scan (isolated/doubled/backward/passed),
  replacing `eval.cpp`'s current `is_passed_pawn()` +
  `passed_pawn_bonus()`.
- A small `PawnHashTable` (private to this file, mirroring
  `TranspositionTable`'s fixed-size, power-of-two, always-replace,
  key-verified-on-probe design from `src/tt.hpp`, but simpler: no
  depth/bound fields, just `{Key key; int mg; int eg;}`, fixed at a
  small constant size, e.g. `1 << 16` entries (~1 MB) — no UCI option,
  no `ucinewgame` clear).
- A file-static `PawnHashTable` instance, probed/filled by
  `pawn_structure()`.

`eval.cpp` drops `is_passed_pawn()`/`passed_pawn_bonus()` and calls
`pawn_structure(pos, mg, eg)` once, adding the result into `mg_score`/
`eg_score` the same way the passed-pawn call does today.

### `Position` changes (`src/position.hpp`/`.cpp`)

- New `zobrist::Key pawn_key_ = 0;` member, alongside `key_`.
- New accessor `zobrist::Key pawn_key() const { return pawn_key_; }`.
- `put_piece()`/`remove_piece()` XOR `zobrist::psq[p][s]` into
  `pawn_key_` too, but only when `type_of(p) == PAWN` — mirroring how
  `mg_psq_`/`eg_psq_` are already updated unconditionally in the same
  choke point, just gated to pawns.

### Definitions

- **Isolated**: no own pawn on file `f-1` or `f+1`, any rank.
- **Doubled**: computed **per file**, not per pawn (avoids
  double-penalizing a stack of 2+): for each file with `n > 1` own
  pawns, apply the penalty `(n-1)` times.
- **Backward**: not passed, **and** no own pawn on an adjacent file at
  the same or less-advanced relative rank (i.e. no neighbor could ever
  support it by advancing), **and** the square directly ahead is
  attacked by an enemy pawn (advancing isn't safe either). All three
  conditions required.

Initial constants (MG/EG, same tapering convention as
`PASSED_PAWN_MG/EG`):

| | MG | EG |
|---|---|---|
| Isolated | -12 | -18 |
| Doubled (per extra pawn on file) | -8 | -16 |
| Backward | -8 | -12 |

## Testing strategy (TDD)

Added to `tests/test_eval.cpp`, following its existing style:

- One hand-built FEN per penalty, isolated to that single feature (e.g.
  a lone isolated pawn with an otherwise passed-pawn-bonus-free
  structure), asserting the exact expected MG/EG delta.
- A file with 3 stacked own pawns, asserting the doubled penalty applies
  `(n-1)` times, not `n` or `n*(n-1)/2` times.
- A backward-pawn FEN where the stop square is enemy-pawn-attacked and
  no neighbor can ever support it, plus a near-miss FEN (neighbor exists
  at equal-or-behind rank) asserting it is *not* flagged backward.
- **Pawn hash consistency test**, mirroring the existing "incremental
  mg_psq/eg_psq/phase match a from-scratch reference"
  (`tests/test_eval.cpp`) and SEE x-ray (`tests/test_see.cpp`) patterns:
  walk a do_move/undo_move tree (captures, promotions, en passant) and
  at every node compare `pawn_structure()`'s (hash-backed) result
  against a from-scratch recomputation that bypasses the hash entirely.
  This covers both `pawn_key_`'s incremental correctness and the hash
  table's key-verified-on-probe collision handling.
- Existing perft and eval tests must remain green.

## Validation

Per `CLAUDE.md`, this changes evaluation output (a search decision, not
a bugfix), so `tools/sprt/run_sprt.sh HEAD` is required after
committing on top of `master`; kept only on "H1 accepted". The whole
package (isolated + doubled + backward + the pawn-hash refactor of
passed pawns) is measured as one SPRT run, since the hash is a pure
cache and does not itself change any score.

If rejected (H0/wash), the fallback is retuning the three constants
once (literature ranges: isolated -8 to -20/-12 to -25, doubled -5 to
-15/-10 to -20, backward -5 to -15/-8 to -18) and re-running SPRT once
more before concluding the feature isn't a net gain and reverting, the
same pattern used for RFP and the abandoned PVS attempt.

No NPS/`bench` gate applies here (per `docs/perf/nps-baseline.md`'s own
scope note) — introducing new eval terms changes the search tree itself
(different pruning/cutoff decisions), so node-count parity is not
expected or required; SPRT alone governs keep/revert.

## Risk notes

- Backward-pawn detection is the most complex of the three and the
  likeliest source of a subtle bug (e.g. off-by-one on "adjacent file,
  equal-or-less-advanced rank"); the dedicated near-miss unit test is
  the main guard.
- Folding `passed_pawn_bonus()` into the new hashed function is a
  refactor of working code, not just an addition — the pawn hash
  consistency test is what proves the passed-pawn number itself didn't
  change in the process (only isolated/doubled/backward are new
  scoring, everything else is a value-preserving move + cache).
- A file-static pawn hash table is shared, mutable global state. It is
  safe under the engine's current single-threaded search; this
  assumption must be revisited (not silently ignored) if a future
  Lazy SMP change makes `evaluate()` reachable from multiple threads
  concurrently.
