# Static Exchange Evaluation (SEE)

## Motivation

The engine currently orders captures with MVV-LVA (victim value minus
attacker value) and searches every capture in quiescence regardless of
whether it actually wins material. Neither reflects what happens if the
target square gets recaptured: MVV-LVA thinks "queen takes pawn" is
always great even when the queen is immediately lost to a defending
pawn, and quiescence spends nodes fully resolving captures that are
obviously losing before the recursion gets a chance to prune them. SEE
statically resolves the full capture sequence on a square (assuming both
sides recapture with their least valuable piece only when it's
profitable) and gives an accurate net material verdict for a capture
without searching it.

Search is otherwise fairly mature (TT, null-move, LMR, aspiration
windows, MultiPV); this is the highest-leverage remaining gap on the
search side, the same way tapered eval was the highest-leverage gap on
the evaluation side.

## Goals

- Add a standalone, independently-testable `see(pos, m)` function.
- Use it to order captures more accurately than MVV-LVA in the main
  search's `order_moves`.
- Use it in quiescence search to skip captures that are known losing
  (`see < 0`), instead of always exploring every capture.

## Non-goals

- No promotion handling in SEE (see Scope cut below).
- No incremental X-ray attacker tracking — attackers are recomputed from
  scratch each step of the exchange for simplicity; this can be
  revisited later if profiling shows it matters.
- No changes to the main negamax move loop itself (no SEE-based pruning
  or reductions there in this pass) — only `order_moves` and
  `quiescence`'s capture filtering are touched.

## Scope cut: promotions

Modeling a promoting pawn's value change mid-exchange (and correctly
handling underpromotions) adds real complexity for a rare case
(promotion *and* capture on the same move). `see()` documents a
precondition that `m` is not a promotion. Both call sites explicitly
bypass SEE for promotion-captures and keep today's behavior for them
(always searched in quiescence, ordered via the existing MVV-LVA score).

## Architecture

New module: `src/see.hpp` / `src/see.cpp`.

```cpp
// Static Exchange Evaluation: net centipawn material result of playing
// out the full capture sequence on to_sq(m), from the perspective of the
// side making move m, assuming both sides always continue with their
// least valuable attacker, but only when doing so doesn't worsen their
// own result.
// Precondition: m is a capture (normal or en passant) and not a
// promotion.
int see(const Position& pos, Move m);
```

### Algorithm (the standard "swap" algorithm)

1. Resolve the initially captured piece's value and square (the en
   passant victim is on a different square than `to_sq(m)`: same rank as
   `from_sq(m)`, same file as `to_sq(m)`).
2. Build a local `Bitboard occ` copy of `pos.occupied()`, with the
   moving piece's origin square (and, for en passant, the captured
   pawn's square) cleared.
3. Compute `attackers_to(pos, to, occ)`: every piece of either color
   that attacks `to` given `occ` (pawns via the reversed-color
   pawn-attack trick, knights/king from their static tables,
   bishops/rooks/queens via the magic-bitboard slider tables, all masked
   by `occ` so already-removed pieces don't count).
4. Repeatedly, for the side to move next: find its least valuable
   attacker still in `attackers_to` (iterate PAWN..KING and take the
   first non-empty bitboard), record the running exchange gain for that
   ply, remove that attacker from `occ`, recompute `attackers_to` (which
   naturally reveals any X-ray attacker behind a removed slider), and
   flip sides. Stop when a side has no attacker left, or when the
   standard pruning check shows continuing can't change the final
   result.
5. Run the backward minimax pass over the recorded per-ply gains (each
   side chooses the better of "stop" vs. "continue") to get the final
   net value.

This is a direct, faithful port of the well-known Chess Programming Wiki
"SEE - The Swap Algorithm" pseudocode, adapted to this codebase's
`Position`/`Bitboard`/`attacks::*` API.

## Integration (`src/search.cpp`)

1. **`order_moves`**: for a capture where `flag_of(m) != PROMOTION`, use
   `see(pos, m)` as the ordering key instead of `mvv_lva_score(pos, m)`
   (still offset by `CAPTURE_BASE`, so captures as a group stay ordered
   above killers/history same as today — only the ordering *within*
   captures changes). Promotion-captures keep using
   `mvv_lva_score`.
2. **`quiescence`**: when filtering the legal move list down to captures
   (the `!checked` branch), also drop non-promotion captures with
   `see(pos, m) < 0`. Promotion-captures and all moves while in check
   are unaffected.

## Testing strategy (TDD)

New file `tests/test_see.cpp`, added to `CMakeLists.txt`'s
`chess_tests` sources. Every position is hand-built (not copied from a
memorized reference test suite, to keep expected values independently
verifiable) with a comment deriving the expected centipawn value by
hand. Write each failing test before the corresponding piece of the
implementation:

- **Free capture**: a queen takes an undefended pawn with no recapture
  possible → `see` returns exactly the pawn's value.
- **Losing capture**: a queen captures a pawn that's defended by a
  single knight and nothing else → the knight recaptures for free
  afterwards, so `see` returns `pawn_value - queen_value` (negative),
  not the naive "material gained" pawn value MVV-LVA would suggest.
- **Even trade stopping correctly**: two equal-value attackers (e.g.
  rook takes rook, defended by exactly one more rook of equal value) →
  `see` returns 0 (trade is even; the exchange doesn't keep escalating
  past that).
- **Pruning / stopping early**: a position where the side to move has a
  further, but worse, continuation available (e.g. recapturing with a
  queen would lose more material than stopping) — asserts the final
  value matches "stop" rather than "always trade down to the last
  attacker."
- **X-ray attacker**: two same-color rooks stacked on the same file
  behind the target square — after the front rook is traded off, the
  second rook must still be counted as recapturing. Verifies
  `attackers_to` recomputation correctly reveals it.
- **En passant**: a capture via en passant where the actual captured
  pawn's square differs from `to_sq(m)`; `see` must clear the correct
  square from `occ` and return the pawn's value when undefended.

Only after `see()` itself is fully covered and passing do the two
`search.cpp` integration points get changed, each validated by the
existing `tests/test_search.cpp` suite continuing to pass (behavioral
regression tests for search itself are out of scope to add here — the
existing tests already exercise move ordering/quiescence indirectly via
perft-adjacent and search-result checks, and correctness of the new
pruning/ordering is what `see()`'s own unit tests establish).

## Validation

Per `CLAUDE.md`, this is a strength-affecting change (not a bugfix), so
after all unit tests pass, it's validated with one SPRT run (ordering
change + quiescence pruning together, mirroring the tapered-eval spec's
precedent of testing a cohesive package as one round) via
`tools/sprt/run_sprt.sh` against current `master`. The change is only
kept if SPRT reports "H1 accepted"; otherwise it's reverted regardless
of how clean the unit tests are.

## Risk notes

- Bundling the ordering change and the quiescence-pruning change into a
  single SPRT run means a regression can't be attributed to one or the
  other without follow-up bisection. Both changes are small and
  low-risk individually (SEE-based ordering is strictly more accurate
  than MVV-LVA; quiescence pruning only ever drops moves already known
  losing), so this mirrors the same trade-off the tapered-eval spec made
  deliberately.
- Recomputing `attackers_to` from scratch every exchange step is
  simpler but does more bitboard work than an incremental X-ray-only
  update; captures are a minority of moves and SEE is only invoked on
  them, so this is expected to be an acceptable cost, but should be
  revisited if SPRT/benchmarking shows a meaningful nodes-per-second
  regression.
