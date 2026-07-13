# Reverse Futility Pruning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Reverse Futility Pruning (static null-move pruning) to `negamax`, cutting shallow-depth nodes whose static evaluation already comfortably exceeds beta, with unit tests proving both the pruning effect and its safety guards, validated by SPRT.

**Architecture:** A single new conditional block inserted into `src/search.cpp`'s `negamax()`, right after `checked` is computed and before the existing null-move pruning block. No new files, no changes to `quiescence`, move ordering, or the transposition table.

**Tech Stack:** C++23, CMake + doctest (`chess_tests` target), fastchess-based SPRT tooling in `tools/sprt/`.

## Global Constraints

- C++23, CMake ≥ 3.20 (already configured; no build-system changes needed).
- Warnings must stay clean: `/W4` on MSVC, `-Wall -Wextra` otherwise.
- Build/test commands: `cmake --build build --config Release`, then
  `./build/Release/chess_tests.exe` (or `ctest --test-dir build --output-on-failure`).
- Per `CLAUDE.md`, this is a search-heuristic strength change, not a bugfix: it may only be
  kept if `tools/sprt/run_sprt.sh` reports **"H1 accepted"**. On a wash or regression, tune
  the constants once and retest; if still rejected, revert.
- All new tests go in the existing `tests/test_search.cpp` (no new test file needed — this is
  a small, single-function-scoped addition, matching that file's existing style: FEN string,
  `SearchLimits`, `TranspositionTable(16)`, `search_best_move`).

---

### Task 1: Implement Reverse Futility Pruning with its tests

**Files:**
- Modify: `src/search.cpp:268` (inside `negamax()`, immediately after `bool checked = in_check(pos);` and before the null-move pruning comment/block)
- Test: `tests/test_search.cpp` (append three new `TEST_CASE`s after the last existing one)

**Interfaces:**
- Consumes: `evaluate(pos)` (from `eval.hpp`, already included in `search.cpp`), `MATE_THRESHOLD` (existing `namespace` constant in `search.cpp`, `= MATE - MAX_DEPTH`), `checked` (existing local `bool` in `negamax`, computed at `src/search.cpp:268`).
- Produces: no new symbols — this is a self-contained conditional early-return inside `negamax`. Nothing else in the plan depends on new names.

- [ ] **Step 1: Write the failing node-count test**

Append to `tests/test_search.cpp`:

```cpp
TEST_CASE("reverse futility pruning cuts nodes in a lopsided, materially winning position") {
    attacks::init();
    // White is up a full queen (Black's queen removed from the normal
    // "r1bqkbnr/..." test position used elsewhere in this file) - White's
    // static eval is far above any reasonable beta at most nodes in the
    // tree, so RFP should fire repeatedly within its depth window.
    Position p; p.set("r1b1kbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = 8;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.nodes < 850000); // current master (no RFP): 900,894 nodes at this depth
}
```

- [ ] **Step 2: Build and run the new test to verify it fails**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="reverse futility pruning cuts nodes in a lopsided, materially winning position"
```

Expected: FAIL — `r.nodes` is 900,894 (unchanged master behavior), not less than 850,000.

- [ ] **Step 3: Write the two regression/safety-net tests**

These document behavior that must *stay* true once RFP exists. They pass
today (nothing has changed yet) and must keep passing after Step 5's
implementation — that's the point: they'd catch a future bug where the
`checked` guard is dropped or misordered, or where RFP corrupts a mate
search. Append both to `tests/test_search.cpp`, after the test from Step 1:

```cpp
TEST_CASE("reverse futility pruning does not fire while in check") {
    attacks::init();
    // White: Ka1 (in check from Ra8 down the open a-file), Qd4, Rh1, Pb2.
    // Black: Ke8, Ra8. White is hugely material-up (queen + rook vs a lone
    // rook), so a naive static eval clears any reasonable beta by a wide
    // margin - but White is in check, and the only legal reply is Ka1-b1
    // (a2 is still on the checked a-file; b2 is occupied by White's own
    // pawn). If RFP ever fired here (ignoring the `checked` guard), the
    // search would return a static-eval-based cutoff instead of searching
    // the forced king move, and could report the wrong "best" move.
    Position p; p.set("r3k3/8/8/8/3Q4/8/1P6/K6R w - - 0 1");
    SearchLimits lim; lim.depth = 3;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_A1, SQ_B1));
}

TEST_CASE("reverse futility pruning does not prevent finding a forced mate") {
    attacks::init();
    // Same mate-in-1 position used by "finds mate in one" above (Re8#),
    // searched at depth 8 - RFP's own depth ceiling - so every node on the
    // path to the mate is inside RFP's active depth window. The
    // `beta < MATE_THRESHOLD` guard must keep RFP from ever cutting off a
    // node that's genuinely hunting a mate score.
    Position p; p.set("6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1");
    SearchLimits lim; lim.depth = 8;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_E1, SQ_E8));
    CHECK(r.score > 29000);
}
```

- [ ] **Step 4: Build and run both new tests to verify they already pass**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="reverse futility pruning does not fire while in check"
./build/Release/chess_tests.exe --test-case="reverse futility pruning does not prevent finding a forced mate"
```

Expected: both PASS (current master already behaves correctly here; RFP
doesn't exist yet to break anything).

- [ ] **Step 5: Implement the minimal RFP block**

In `src/search.cpp`, find this existing code (around line 268):

```cpp
    bool checked = in_check(pos);

    // Null-move pruning: if we could pass the turn entirely and the
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
    // `beta`, consistent with quiescence's own fail-soft standing pat.
    constexpr int RFP_MAX_DEPTH = 8;
    constexpr int RFP_MARGIN = 120;
    if (!checked && depth <= RFP_MAX_DEPTH && beta < MATE_THRESHOLD) {
        int eval = evaluate(pos);
        int margin = RFP_MARGIN * depth;
        if (eval - margin >= beta)
            return eval - margin;
    }

    // Null-move pruning: if we could pass the turn entirely and the
```

- [ ] **Step 6: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — all three new tests (including the Step 1 node-count test,
now that RFP exists) plus every pre-existing test in the suite (in
particular, the rest of `test_search.cpp`'s mate/tactics/repetition/TT
tests, which exercise correctness broadly enough to catch a badly broken
RFP condition).

- [ ] **Step 7: Commit**

```bash
git add src/search.cpp tests/test_search.cpp
git commit -m "$(cat <<'EOF'
feat: add reverse futility pruning to negamax

At shallow remaining depth, if the static eval already clears beta by
more than a per-ply margin, cut the node immediately instead of
recursing - skipped in check and near mate scores, mirroring null-move
pruning's own guards.
EOF
)"
```

---

### Task 2: Validate with SPRT

**Files:** none (measurement only; may produce a follow-up commit if constants need retuning, or a revert commit if the feature doesn't hold up).

**Interfaces:**
- Consumes: `tools/sprt/run_sprt.sh` (existing script; see `tools/sprt/README.md`), the Task 1 commit as the SPRT candidate.
- Produces: nothing further tasks depend on — this is the final gate for the feature.

- [ ] **Step 1: Confirm SPRT tooling is set up**

```bash
ls tools/sprt/fastchess tools/sprt/books
```

Expected: both directories exist and are non-empty. If either is missing, run the one-time setup first:

```bash
tools/sprt/setup.sh
```

- [ ] **Step 2: Run the SPRT match**

`HEAD` is Task 1's commit; `HEAD~1` is the commit immediately before it (the RFP design-spec commit, i.e. current master before this feature) — so this compares the RFP candidate against master without RFP:

```bash
tools/sprt/run_sprt.sh HEAD~1 HEAD
```

This runs until fastchess's LLR crosses a bound (or `SPRT_ROUNDS` is exhausted) and prints one of:
- `H1 accepted` — the candidate (with RFP) is a confirmed strength gain.
- `H0 accepted` — no gain, or a regression.

This can take anywhere from several minutes to a couple of hours depending
on hardware (default time control `10+0.1`, `SPRT_CONCURRENCY=2`); it's
safe to run in the background and check back on the output.

- [ ] **Step 3a: If "H1 accepted" — done**

No further action. The Task 1 commit stands as-is; the feature is validated and kept.

- [ ] **Step 3b: If "H0 accepted" (wash or regression) — retune once**

Edit the two constants in `src/search.cpp` (from Step 5 of Task 1) to a
less aggressive margin and shallower depth ceiling:

```cpp
    constexpr int RFP_MAX_DEPTH = 6;
    constexpr int RFP_MARGIN = 150;
```

Rebuild, rerun the full test suite (the Task 1 tests use thresholds tuned
to the original constants — the node-count test's `850000` bound may need
loosening if the shallower/wider margin prunes less; rerun it first and
adjust only that one number if it now fails, since a smaller `RFP_MAX_DEPTH`
legitimately means less pruning):

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Commit as a follow-up:

```bash
git add src/search.cpp tests/test_search.cpp
git commit -m "$(cat <<'EOF'
tune: reduce reverse futility pruning aggressiveness

First attempt (margin 120/ply, depth<=8) didn't clear SPRT; narrowing
the depth window and widening the margin trades pruning volume for
safety.
EOF
)"
```

Then repeat Step 2 (`tools/sprt/run_sprt.sh HEAD~1 HEAD`, still comparing
against the same pre-RFP baseline). If this second attempt also reports
"H0 accepted", the feature isn't a net gain for this engine — revert both
commits and stop:

```bash
git revert --no-edit HEAD HEAD~1
```
