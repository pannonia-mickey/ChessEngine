# Tapered, positional evaluation upgrade

## Motivation

`src/eval.cpp` currently scores a position as material value + a single
static piece-square table (PST) per piece type, used unchanged from the
opening to the deepest endgame. The search side of the engine (iterative
deepening, TT, null-move pruning, LMR, aspiration windows, MultiPV, killer/
history move ordering) is comparatively mature; evaluation is the
biggest remaining gap and, on a codebase this bare, the highest-leverage
place to add strength.

This change replaces the single PST with a middlegame/endgame (MG/EG)
tapered scheme and adds five well-established positional terms: mobility,
bishop pair, rook on open/semi-open file, passed pawns, and king safety
(pawn shield).

## Goals

- Introduce game-phase-aware evaluation (MG/EG interpolation) so piece
  placement values can differ appropriately between the opening and the
  endgame (e.g. king centralization matters in the endgame but not the
  middlegame).
- Add mobility, bishop pair, rook file, passed pawn, and king safety
  terms, each independently unit-testable.
- Keep `evaluate(const Position&)`'s signature and side-to-move-relative,
  centipawn contract unchanged; this is purely an internal rework of
  `eval.cpp`.

## Non-goals

- No tuning/self-play parameter optimization (e.g. Texel tuning) in this
  pass — values are taken from well-established published sources or
  common magnitudes used across open-source engines, then validated in
  bulk by SPRT (see Validation).
- No further positional terms beyond the five listed (e.g. no pawn
  chains/outposts/space) — those are candidates for a future pass.
- No changes to search.cpp.

## Architecture

### Game phase

```
constexpr int PHASE_WEIGHT[PIECE_TYPE_NB] = {0, 1, 1, 2, 4, 0}; // P,N,B,R,Q,K
constexpr int PHASE_MAX = 24; // 2*(1+1+2)+2*4 per side... i.e. full material
```

`game_phase(pos)` sums `PHASE_WEIGHT[pt]` over every piece currently on the
board and clamps to `[0, PHASE_MAX]` (a position with extra material via
promotion should not overflow the taper). Phase 24 = full non-pawn
material (opening); phase 0 = bare kings/pawns (deep endgame).

### Tapering

Material values and PSTs are split into MG and EG tables (values based on
the public-domain "PeSTO" evaluation tables, which extend the same
simplified-PST style already used in this codebase, with a separate table
per phase). Each side accumulates an `mg_score` and `eg_score` from
material + PST; the blend is:

```
taper(mg, eg, phase) = (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX
```

Passed-pawn bonuses (by rank) also use separate MG/EG tables and are
folded into `mg_score`/`eg_score` before tapering, since passed pawns are
worth more as material comes off the board.

King safety (pawn shield) is added to `mg_score` only (implicitly zero
weight in the endgame taper), since it stops mattering once there isn't
enough attacking material left to exploit an open king.

Mobility, bishop pair, and rook-file bonuses are phase-independent flat
bonuses, added to the final blended score (no single canonical published
MG/EG split exists for these, unlike PST/passed-pawns).

### Final formula

```
score(color) = taper(mg_score, eg_score, phase)
             + mobility(color) + bishop_pair(color) + rook_file_bonus(color)
score = score(WHITE) - score(BLACK)
return side_to_move == BLACK ? -score : score
```

## Components

Each term is a small free function in `eval.cpp`, taking `(const
Position&, Color)` and returning that color's contribution in
centipawns, so each can be called for WHITE and BLACK and subtracted —
and unit-tested independently via minimal positions where every other
term cancels by symmetry or by having no pieces of the relevant kind.

1. **`game_phase(pos) -> int`** — described above.
2. **MG/EG material + PST accumulation** — replaces the current single
   `MATERIAL_VALUE`/`PST` loop; produces `mg_score`/`eg_score` per side.
3. **`mobility(pos, color) -> int`** — for each knight/bishop/rook/queen
   of `color`, count attacked squares (via `attacks::*_attacks`, using
   full board occupancy) not occupied by own pieces, times a per-piece-
   type weight (knight/bishop weighted higher than rook/queen, as
   knight/bishop mobility is a stronger positional signal per attacked
   square — standard in simplified mobility schemes). Pawns and king are
   excluded (their "mobility" isn't a meaningful positional signal here).
4. **`bishop_pair(pos, color) -> int`** — flat bonus if `popcount(pieces(color, BISHOP)) >= 2`.
5. **`rook_file_bonus(pos, color) -> int`** — per rook of `color`: bonus
   if its file has no pawns of either color (open), smaller bonus if it
   has only enemy pawns (semi-open), zero otherwise.
6. **`passed_pawn_bonus(pos, color) -> (mg, eg)`** — per pawn of `color`:
   passed if no enemy pawn occupies the same file or an adjacent file at
   or ahead of it (in `color`'s direction of travel); bonus by rank from
   MG/EG tables (EG values larger).
7. **`king_safety(pos, color) -> int`** (MG-only) — count `color`'s own
   pawns on the two ranks immediately in front of its king, across the
   king's file and the two adjacent files (clamped at board edges); bonus
   per shielding pawn found.

## Testing strategy (TDD)

All tests live in `tests/test_eval.cpp` (existing file, already
registered in `CMakeLists.txt`). Write each failing test before its
implementation, per component:

- **Phase/tapering**: a bare-king(+extra piece) endgame position must
  prefer a centralized king over a cornered one — the inverse of the
  current (MG-only) king PST behavior. The existing test "king PST pins
  exact castled/exposed values" is rewritten: with only kings on the
  board (phase 0, pure EG table), centralization must score higher than
  the corner, replacing the old exact-value pin (which encoded the old,
  now-intentionally-changed MG-only behavior).
- **Mobility**: a knight in the center of an otherwise empty board scores
  higher than the same knight boxed into a corner by its own pawns, all
  else equal.
- **Bishop pair**: a side with two bishops scores higher than the same
  side with one bishop swapped for a knight of equal count, isolating the
  pair bonus by keeping material as close to equal as the PeSTO values
  allow and asserting the direction/rough magnitude, not an exact number.
- **Rook file**: a rook on a fully open file scores higher than one on a
  file with a blocking own pawn, all else equal.
- **Passed pawn**: an unopposed, unblockable pawn scores higher than the
  same pawn with an enemy pawn able to capture or block it on the same or
  adjacent file; a more advanced passed pawn scores higher than a less
  advanced one.
- **King safety**: a king with an intact 3-pawn shield scores higher than
  the same king with the shield pawns pushed or missing, all else equal.

Each new test isolates its term with a minimal, close-to-symmetric
position so unrelated terms cancel out or don't apply (e.g. king-only
positions for phase, single-knight positions for mobility).

## Validation

Per `CLAUDE.md`, evaluation-weight changes affect playing strength in a
way that isn't decided up front, so after the TDD implementation passes
all unit tests, the whole change is validated in one SPRT run (all five
terms plus tapering together, per the user's explicit choice to test the
full package in a single round rather than incrementally) via
`tools/sprt/run_sprt.sh` against current `master`. The change is only
kept if SPRT reports "H1 accepted"; otherwise it's reverted regardless of
how clean the unit tests are, since unit tests only check each term's
direction/isolation, not its net effect on playing strength.

## Risk notes

- Bundling all five terms in one SPRT run means a regression or wash
  can't be attributed to a single term without follow-up bisection (the
  user explicitly chose this trade-off over incremental SPRT rounds).
- The MG/EG PST values are a best-effort reproduction of the publicly
  known "PeSTO" tables from memory, not copy-pasted from a source file;
  exact numbers may differ slightly from the original publication. This
  is acceptable since correctness here means "reasonable, well-attested
  magnitudes that SPRT validates," not "bit-exact match to a reference
  implementation."
