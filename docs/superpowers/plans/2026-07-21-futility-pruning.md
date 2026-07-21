# Futility Pruning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a per-move futility pruning check to `negamax`'s main move loop, skipping quiet moves at shallow remaining depth when the static eval plus a depth-scaled margin still can't reach `alpha`, with unit tests proving the pruning effect and its safety guards, validated by SPRT.

**Architecture:** `negamax()`'s existing reverse futility pruning (RFP) block already computes a static eval gated by `!checked && depth <= RFP_MAX_DEPTH`. That eval computation is hoisted one level so both RFP and the new futility check share it (Task 1, a pure no-op refactor). The futility check itself is then added as a new per-move skip inside the main move loop, positioned right after `gives_check` is computed — the same point LMR already makes its own per-move decision (Task 2).

**Tech Stack:** C++23, CMake + doctest (`chess_tests` target), fastchess-based SPRT tooling in `tools/sprt/`.

## Global Constraints

- C++23, CMake ≥ 3.20 (already configured; no build-system changes needed).
- Warnings must stay clean: `/W4` on MSVC, `-Wall -Wextra` otherwise.
- Build/test commands: `cmake --build build --config Release`, then
  `./build/Release/chess_tests.exe` (or `ctest --test-dir build --output-on-failure`).
- Per `CLAUDE.md`, Task 1 is a **pure speed-neutral refactor** (no search-tree change): it may
  only be kept if `./build/Release/chess_engine.exe bench` reports the exact same
  **`Nodes searched : 78404283`** before and after — that's today's baseline on this machine,
  captured fresh from current `master`.
- Per `CLAUDE.md`, Task 2 is a **search-heuristic strength change**, not a bugfix: it may only
  be kept if `tools/sprt/run_sprt.sh` reports **"H1 accepted"**. On a wash or regression, tune
  the constants once and retest; if still rejected, revert.
- All new tests go in the existing `tests/test_search.cpp` (no new test file needed — this is
  a small, single-function-scoped addition, matching that file's existing style: FEN string,
  `SearchLimits`, `TranspositionTable(16)`, `search_best_move`).

---

### Task 1: Hoist RFP's static eval computation

**Files:**
- Modify: `src/search.cpp:311-330` (inside `negamax()`, the `checked` line through the end of
  the existing RFP block)

**Interfaces:**
- Consumes: `evaluate(pos)` (from `eval.hpp`, already included), `MATE_THRESHOLD` (existing
  `namespace` constant), `checked` (existing local `bool`).
- Produces: a new local `int eval` in `negamax`, valid (computed via `evaluate(pos)`) whenever
  `!checked && depth <= RFP_MAX_DEPTH`, else `0`. Task 2 consumes this variable and widens its
  guard condition.

- [ ] **Step 1: Record today's bench baseline**

```bash
cmake --build build --config Release --target chess_engine
./build/Release/chess_engine.exe bench
```

Expected tail output:
```
Nodes searched  : 78404283
```

If this machine produces a different number than `78404283`, use *that* number as the
before/after comparison target for Step 3 instead — the important property is that Step 1's
and Step 3's numbers match exactly, not that they match this document.

- [ ] **Step 2: Apply the refactor**

In `src/search.cpp`, find this existing code:

```cpp
    bool checked = in_check(pos);

    // Reverse futility pruning (static null-move pruning): at shallow
    // remaining depth, if the static eval already clears beta by more than
    // a per-ply safety margin, the position is comfortably winning enough
    // that searching further is very unlikely to change the outcome, so
    // the node is cut immediately without recursing. Skipped in check (the
    // static eval is unreliable there) and near mate scores (same
    // reasoning as null-move pruning's own guard below). Fail-soft: returns
    // the actual (eval - margin) lower bound rather than the coarser
    // `beta`, consistent with this same move loop's own fail-soft `best`
    // return below, which likewise can exceed `beta` on a cutoff.
    constexpr int RFP_MAX_DEPTH = 8;
    constexpr int RFP_MARGIN = 120;
    if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
        int eval = evaluate(pos);
        int margin = RFP_MARGIN * depth;
        if (eval - margin >= beta)
            return eval - margin;
    }
```

Replace it with:

```cpp
    bool checked = in_check(pos);

    // Reverse futility pruning (static null-move pruning): at shallow
    // remaining depth, if the static eval already clears beta by more than
    // a per-ply safety margin, the position is comfortably winning enough
    // that searching further is very unlikely to change the outcome, so
    // the node is cut immediately without recursing. Skipped in check (the
    // static eval is unreliable there) and near mate scores (same
    // reasoning as null-move pruning's own guard below). Fail-soft: returns
    // the actual (eval - margin) lower bound rather than the coarser
    // `beta`, consistent with this same move loop's own fail-soft `best`
    // return below, which likewise can exceed `beta` on a cutoff.
    constexpr int RFP_MAX_DEPTH = 8;
    constexpr int RFP_MARGIN = 120;
    int eval = 0;
    if (!checked && depth <= RFP_MAX_DEPTH)
        eval = evaluate(pos);
    if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
        int margin = RFP_MARGIN * depth;
        if (eval - margin >= beta)
            return eval - margin;
    }
```

This is a pure hoist: `eval` moves from an inner scope to an outer one, computed under a
slightly broader (but currently equivalent-in-effect) guard. It changes no search decision —
`evaluate(pos)` has no side effects and `eval`'s value is unused when `beta >= MATE_THRESHOLD`
— so it cannot change which nodes get visited.

- [ ] **Step 3: Rebuild and confirm bench parity**

```bash
cmake --build build --config Release --target chess_engine
./build/Release/chess_engine.exe bench
```

Expected: `Nodes searched` is **exactly** the Step 1 value (`78404283` on this machine). If it
differs by even one node, stop — the refactor is not actually behavior-preserving, and the
diff needs to be re-examined before proceeding (do not treat this as a normal SPRT-gated
change; a node-count mismatch here means a logic error, not a strength tradeoff).

- [ ] **Step 4: Run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — every existing test, in particular the three RFP tests in
`tests/test_search.cpp` (`reverse futility pruning cuts nodes...`, `...does not fire while in
check`, `...does not prevent finding a forced mate`), unaffected by the refactor.

- [ ] **Step 5: Commit**

```bash
git add src/search.cpp
git commit -m "$(cat <<'EOF'
refactor: hoist RFP's static eval for reuse by futility pruning

Pure no-op hoist (bench-verified identical node count): moves the
evaluate(pos) call one scope up so an upcoming futility pruning check
can share it instead of calling evaluate(pos) a second time per node.
EOF
)"
```

---

### Task 2: Implement futility pruning with its tests

**Files:**
- Modify: `src/search.cpp` (Task 1's hoisted block, and the main move loop)
- Test: `tests/test_search.cpp` (append four new `TEST_CASE`s after the last existing one)

**Interfaces:**
- Consumes: `eval` (from Task 1), `MATE_THRESHOLD`, `checked`, `move_index`, `tt_move`,
  `capture`, `mf` (`MoveFlag`), `gives_check` (all existing locals in `negamax`'s move loop).
- Produces: no new symbols outside `negamax` — this is a self-contained addition. Nothing else
  in this plan depends on new names.

- [ ] **Step 1: Write the failing node-count test**

Append to `tests/test_search.cpp`:

```cpp
TEST_CASE("futility pruning cuts nodes in a hopeless quiet position") {
    attacks::init();
    // Own zobrist::init() call, matching the RFP node-count test's own
    // reasoning above: this assertion pins an exact node-count threshold,
    // which is sensitive to TT hash-slot aliasing and therefore to exactly
    // which zobrist keys are loaded.
    zobrist::init();
    // White is down a full queen against a fully-developed Black army (the
    // normal "r1bqkbnr/..." test position used elsewhere in this file,
    // with White's queen removed) - White's static eval is far below any
    // reasonable alpha at most nodes in the tree, and there are plenty of
    // quiet king/piece shuffles available to prune, so futility pruning
    // should fire repeatedly within its depth window.
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNB1K2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = 6;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.nodes < 55000); // current master (no futility pruning): 65,699 nodes at this depth
}
```

- [ ] **Step 2: Build and run the new test to verify it fails**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="futility pruning cuts nodes in a hopeless quiet position"
```

Expected: FAIL — `r.nodes` is 65,699 (unchanged master behavior), not less than 55,000.

- [ ] **Step 3: Write the three safety-net tests**

These document behavior that must *stay* true once futility pruning exists. They already pass
today (nothing has changed yet) and must keep passing after Step 5's implementation — that's
the point: they'd catch a future bug where a guard is dropped, misordered, or where the
capture/promotion/check exemption is lost. Append all three to `tests/test_search.cpp`, after
the test from Step 1:

```cpp
TEST_CASE("futility pruning does not fire while in check") {
    attacks::init();
    // Same position as the RFP "does not fire while in check" test: White
    // Ka1 is in check from Ra8, hugely material-up (queen + rook vs a lone
    // rook) so a naive static eval clears any reasonable alpha by a wide
    // margin, but Ka1-b1 is the only legal reply (a2 is still on the
    // checked a-file; b2 is occupied by White's own pawn). If futility
    // pruning ever fired here (ignoring the `checked` guard), the search
    // could skip that forced reply and report the wrong "best" move.
    Position p; p.set("r3k3/8/8/8/3Q4/8/1P6/K6R w - - 0 1");
    SearchLimits lim; lim.depth = 3;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_A1, SQ_B1));
}

TEST_CASE("futility pruning never skips a capture, promotion, or checking move") {
    attacks::init();
    zobrist::init();
    // White: Ke1, Nc3, Rh1 (up a rook and a knight). Black: Ke8, Pb2 - one
    // step from promoting on b1, an empty square (a non-capturing
    // promotion, so move ordering doesn't already front-load it the way a
    // capture would). Root forced to Ke1-d2 via search_moves so Black's
    // decision happens at ply 1, inside negamax's internal move loop where
    // futility applies (the root's own move loop never prunes). Without
    // the "never skip a promotion" exemption, futility pruning would treat
    // b2-b1=Q as just another quiet move worth skipping once alpha is
    // established, corrupting the search's view of Black's best defense
    // and changing the reported score.
    Position p; p.set("4k3/8/8/8/8/2N5/1p6/4K2R w K - 0 1");
    SearchLimits lim; lim.depth = 4;
    lim.search_moves = {make_move(SQ_E1, SQ_D2)};
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    // Measured value with the exemption present (== today's pre-futility
    // value too, since a correct exemption never changes this particular
    // line's outcome); an exemption that's missing or narrower than
    // "capture, promotion, or check" measurably changes this to 776.
    CHECK(r.score == 803);
}

TEST_CASE("futility pruning does not prevent finding a forced mate") {
    attacks::init();
    // Same mate-in-1 position used by "finds mate in one" above (Re8#),
    // searched at depth 8 - futility pruning's own depth ceiling - so
    // every node on the path to the mate is inside its active depth
    // window. The `alpha > -MATE_THRESHOLD` guard must keep futility
    // pruning from ever skipping a move that's genuinely hunting a mate
    // score.
    Position p; p.set("6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1");
    SearchLimits lim; lim.depth = 8;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_E1, SQ_E8));
    CHECK(r.score > 29000);
}
```

- [ ] **Step 4: Build and run all three to verify they already pass**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="futility pruning does not fire while in check"
./build/Release/chess_tests.exe --test-case="futility pruning never skips a capture, promotion, or checking move"
./build/Release/chess_tests.exe --test-case="futility pruning does not prevent finding a forced mate"
```

Expected: all three PASS (current master already behaves correctly here; futility pruning
doesn't exist yet to break anything).

- [ ] **Step 5: Implement the minimal futility pruning logic**

In `src/search.cpp`, find Task 1's hoisted block:

```cpp
    constexpr int RFP_MAX_DEPTH = 8;
    constexpr int RFP_MARGIN = 120;
    int eval = 0;
    if (!checked && depth <= RFP_MAX_DEPTH)
        eval = evaluate(pos);
    if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
        int margin = RFP_MARGIN * depth;
        if (eval - margin >= beta)
            return eval - margin;
    }
```

Replace it with (widening the `eval` guard to cover futility pruning's own depth ceiling, and
adding the futility setup used by the move loop below):

```cpp
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

    // Futility pruning: at shallow remaining depth, if the static eval
    // plus a depth-scaled margin still can't reach alpha, a quiet move
    // here is extremely unlikely to be the best move, so it's skipped
    // without recursing into it. Companion to RFP above (same "static eval
    // is a reliable-enough proxy at shallow depth" assumption, applied
    // against alpha instead of beta, per move instead of once per node).
    // Never skips the first move at a node (there must always be at least
    // one fully-searched move), the TT move, or a capture/promotion/check
    // (all of which can swing eval by more than a flat margin predicts).
    bool futility_possible = !checked && depth <= FUTILITY_MAX_DEPTH && alpha > -MATE_THRESHOLD;
    int futility_score = eval + FUTILITY_MARGIN * depth;
```

Then find the main move loop's per-move setup:

```cpp
        pos.do_move(m, st);
        history.push_back(pos.key());
        bool gives_check = in_check(pos);

        int score;
```

Replace it with:

```cpp
        pos.do_move(m, st);
        history.push_back(pos.key());
        bool gives_check = in_check(pos);

        // Decided here (after do_move, not before) so it can reuse
        // gives_check the same way LMR's own per-move decision does below,
        // instead of needing a new static "does this move give check"
        // predicate.
        if (futility_possible && move_index > 0 && m != tt_move && !capture &&
            mf != PROMOTION && !gives_check && futility_score <= alpha) {
            history.pop_back();
            pos.undo_move(m, st);
            continue;
        }

        int score;
```

- [ ] **Step 6: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — all four new tests (including the Step 1 node-count test, now that futility
pruning exists) plus every pre-existing test in the suite, in particular the rest of
`test_search.cpp`'s mate/tactics/repetition/TT tests and the three RFP tests, which exercise
correctness broadly enough to catch a badly broken futility condition.

- [ ] **Step 7: Commit**

```bash
git add src/search.cpp tests/test_search.cpp
git commit -m "$(cat <<'EOF'
feat: add futility pruning to negamax

At shallow remaining depth, skip a quiet move in the main move loop
if the static eval plus a depth-scaled margin still can't reach
alpha - never skips the first move at a node, the TT move, or a
capture/promotion/check. Companion to the existing reverse futility
pruning, applied per move against alpha instead of once per node
against beta.
EOF
)"
```

---

### Task 3: Validate with SPRT

**Files:** none (measurement only; may produce a follow-up commit if constants need retuning,
or a revert commit if the feature doesn't hold up).

**Interfaces:**
- Consumes: `tools/sprt/run_sprt.sh` (existing script; see `tools/sprt/README.md`), Task 2's
  commit as the SPRT candidate.
- Produces: nothing further tasks depend on — this is the final gate for the feature.

- [ ] **Step 1: Confirm SPRT tooling is set up**

```bash
ls tools/sprt/fastchess tools/sprt/books
```

Expected: both directories exist and are non-empty. If either is missing, run the one-time
setup first:

```bash
tools/sprt/setup.sh
```

- [ ] **Step 2: Run the SPRT match**

`HEAD` is Task 2's commit; `HEAD~1` is Task 1's commit (the pure refactor, bench-verified
node-count-identical to today's master) — so this isolates futility pruning's own effect,
uncontaminated by the refactor:

```bash
tools/sprt/run_sprt.sh HEAD~1 HEAD
```

This runs until fastchess's LLR crosses a bound (or `SPRT_ROUNDS` is exhausted) and prints one
of:
- `H1 accepted` — the candidate (with futility pruning) is a confirmed strength gain.
- `H0 accepted` — no gain, or a regression.

This can take anywhere from several minutes to a couple of hours depending on hardware
(default time control `10+0.1`, `SPRT_CONCURRENCY=2`); it's safe to run in the background and
check back on the output.

- [ ] **Step 3a: If "H1 accepted" — done**

No further action. The Task 2 commit stands as-is; the feature is validated and kept.

- [ ] **Step 3b: If "H0 accepted" (wash or regression) — retune once**

Edit the two constants in `src/search.cpp` (from Step 5 of Task 2) to a less aggressive
margin:

```cpp
    constexpr int FUTILITY_MAX_DEPTH = 6;
    constexpr int FUTILITY_MARGIN = 220;
```

Rebuild, rerun the full test suite (Task 2's node-count test threshold of `55000` was tuned to
the original constants — a shallower `FUTILITY_MAX_DEPTH` and wider margin legitimately means
less pruning, so rerun that one test first and loosen its threshold if it now fails; leave the
other three tests alone, since they're guard/correctness checks independent of the exact
margin):

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Commit as a follow-up:

```bash
git add src/search.cpp tests/test_search.cpp
git commit -m "$(cat <<'EOF'
tune: reduce futility pruning aggressiveness

First attempt (margin 150/ply, depth<=8) didn't clear SPRT; narrowing
the depth window and widening the margin trades pruning volume for
safety.
EOF
)"
```

Then repeat Step 2 (`tools/sprt/run_sprt.sh HEAD~1 HEAD`, still comparing against the same
pre-futility-pruning baseline). If this second attempt also reports "H0 accepted", the feature
isn't a net gain for this engine — revert both commits and stop:

```bash
git revert --no-edit HEAD HEAD~1
```
