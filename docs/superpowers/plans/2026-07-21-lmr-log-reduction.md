# LMR Log-Based Reduction Table Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `negamax`'s flat 1-ply late move reduction with a log-based
`depth × move_index` reduction table, preserving today's decisions at shallow
depth/early move index and scaling more aggressively beyond that, validated by SPRT.

**Architecture:** A new private helper `lmr_reduction(depth, move_index)` is added to
`search.cpp`'s anonymous namespace, backed by a function-local `static const`
`[MAX_DEPTH + 1][256]` lookup table built once via `0.75 + log(depth)*log(move_index)
/ 2.25`, floored at 1. The existing LMR branch (`do_lmr` block) swaps its flat
`depth - 2` for `std::max(1, depth - 1 - lmr_reduction(depth, move_index))`. The probe
→ conditional full re-search structure, and `do_lmr`'s eligibility condition, are
untouched.

**Tech Stack:** C++23, CMake + doctest (`chess_tests` target), fastchess-based SPRT
tooling in `tools/sprt/`.

## Global Constraints

- C++23, CMake ≥ 3.20 (already configured; no build-system changes needed).
- Warnings must stay clean: `/W4` on MSVC, `-Wall -Wextra` otherwise.
- Build/test commands: `cmake --build build --config Release`, then
  `./build/Release/chess_tests.exe` (or `ctest --test-dir build --output-on-failure`).
- Per `CLAUDE.md`, this is a **search-heuristic strength change**, not a bugfix or a
  pure speed change: unit tests must pass, but the change may only be *kept* if
  `tools/sprt/run_sprt.sh` reports **"H1 accepted"**. On a wash or regression, retune
  the two constants once and re-test; if still rejected, revert.
- New tests go in the existing `tests/test_search.cpp` (matching its style: FEN
  string, `SearchLimits`, `TranspositionTable(16)`, `search_best_move`).

---

### Task 1: Add the log-based reduction table and wire it into the LMR branch

**Files:**
- Modify: `src/search.cpp` (top-of-file includes, anonymous namespace, and the
  `do_lmr` block inside `negamax`'s move loop, currently `search.cpp:405-412`)
- Test: `tests/test_search.cpp` (append one new `TEST_CASE` after the last existing
  one)

**Interfaces:**
- Consumes: `MAX_DEPTH` (existing constant from `search.hpp`), `<array>`, `<cmath>`
  (new includes).
- Produces: `int lmr_reduction(int depth, int move_index)`, a new private helper in
  `search.cpp`'s anonymous namespace. Nothing outside `search.cpp` depends on it.

- [x] **Step 1: Measure today's baseline for the regression test**

Already measured for this plan (recorded here so the test in Step 2 doesn't need to
re-derive it): on current `master`, position
`r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq -` at `depth = 6` returns
**best move `e1g1` (O-O), score `59`, `53,890` nodes**.

If re-measuring on a different machine/commit, use a throwaway `TEST_CASE` with
`MESSAGE("best=", to_sq(r.best), " from=", from_sq(r.best), " score=", r.score,
" nodes=", r.nodes);` run via `chess_tests.exe --test-case="..." -s`, then discard the
throwaway test — do not leave it in the suite.

- [x] **Step 2: Write the regression test (fails only if a future refactor breaks
  decision correctness — currently passes, since no code has changed yet)**

Append to `tests/test_search.cpp` (needs `#include <cstdlib>` added alongside the
existing `<atomic>`/`<chrono>`/`<thread>` includes at the top of the file, for
`std::abs`):

```cpp
TEST_CASE("LMR log-based reduction preserves the pre-change best move within a small score tolerance") {
    attacks::init();
    zobrist::init();
    // A middlegame-ish position with enough legal moves and search depth that
    // LMR reduces many quiet moves across a range of move indices (this file's
    // usual "r1bqkbnr/..." position, also used by several MultiPV/on_iteration
    // tests above). Unlike PVS (provably equivalent to full-window alpha-beta),
    // LMR is a genuine heuristic: a reduced-depth result that doesn't beat
    // alpha is trusted as-is, never re-searched, so changing the reduction
    // magnitude can legitimately shift the exact backed-up score by a few
    // centipawns even when the decision (which move to play) doesn't change -
    // this is expected, not a correctness bug (that's exactly why CLAUDE.md
    // requires SPRT, not bit-exact score preservation, for this kind of
    // change). Baseline measured on master before this change: best e1g1
    // (O-O), score 59, 53,890 nodes; after adding the log-based reduction
    // table, score 60 - same move, 1 cp drift. The tolerance below is wide
    // enough to absorb that kind of expected drift while still catching a
    // gross correctness bug (e.g. a broken re-search condition), which would
    // be expected to swing the score by far more than a few centipawns.
    Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
    SearchLimits lim; lim.depth = 6;
    TranspositionTable tt(16);
    SearchResult r = search_best_move(p, lim, tt);
    CHECK(r.best == make_move(SQ_E1, SQ_G1, CASTLING));
    CHECK(std::abs(r.score - 59) <= 10);
}
```

(Measured while executing this plan: the exact score did shift by 1 cp — 59 → 60 —
after the change, confirming the tolerance above, not exact equality, is the right
assertion; see the in-test comment.)

- [x] **Step 3: Build and run the new test to confirm it passes against current
  master (sanity check before touching `negamax`)**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="LMR log-based reduction preserves the pre-change best move within a small score tolerance"
```

Expected: PASS (nothing has changed yet; this just confirms the recorded baseline in
Step 1 was transcribed correctly). (Note: the first transcription used
`make_move(SQ_E1, SQ_G1)` without the `CASTLING` flag and failed — `e1g1` here is a
castling move, which needs `make_move(SQ_E1, SQ_G1, CASTLING)` to compare equal;
fixed before proceeding.)

- [x] **Step 4: Add the reduction table helper**

In `src/search.cpp`, add the two new includes near the top (alongside the existing
`<algorithm>`, `<chrono>`, `<utility>`, `<vector>`):

```cpp
#include <array>
#include <cmath>
```

Then, in the anonymous namespace, add `lmr_reduction` right before `negamax`'s
definition (after the `MAX_CHECK_EXT`/`quiescence` block, so it's adjacent to the
other move-loop-support helpers like `mvv_lva_score`/`move_order_score` above):

```cpp
// Log-based LMR reduction: scales with both remaining depth and how far down
// the ordered move list a move sits, cutting harder the deeper and later a
// move is (later moves are progressively less likely to be best, per move
// ordering - see the LMR eligibility comment below, at the do_lmr branch). At
// depth=3, move_index=4 (LMR's own minimum eligibility point) this evaluates
// to r=1, i.e. exactly today's flat depth-2 behavior; it only diverges
// (larger r) as depth and/or move_index grow past that point. Base (0.75) and
// divisor (2.25) are literature-typical starting constants - see the design
// spec (docs/superpowers/specs/2026-07-21-lmr-log-reduction-design.md) for the
// retuning fallback if SPRT rejects them.
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

- [x] **Step 5: Wire the table into the `do_lmr` branch**

In `src/search.cpp`, find (currently around line 405-412):

```cpp
        bool do_lmr = depth >= 3 && move_index >= 4 && !capture &&
                      mf != PROMOTION && !gives_check && m != tt_move;
        if (do_lmr) {
            score = -negamax(pos, depth - 2, -alpha - 1, -alpha, ply + 1,
                              nodes, tg, tt, tables, history);
            if (score > alpha)
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                                  nodes, tg, tt, tables, history);
        } else {
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tg, tt, tables, history);
        }
```

Replace it with:

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

- [x] **Step 6: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — every existing test (in particular the mate-in-N tests, the tactical
tests, and the new regression test from Step 2), unaffected in outcome by the
reduction-magnitude change. If the new regression test now fails on the *best move*
(not just the score, which is expected to drift within tolerance — see Step 2's
comment), stop and re-examine before proceeding: that would indicate a bug in the
re-search condition or the floor, not just an expected score perturbation.

Actual result: 136/136 test cases passed, including the new regression test (best
move `e1g1` castling preserved; score 60, within the ±10 cp tolerance of the
pre-change baseline of 59). `ctest --test-dir build --output-on-failure` also
confirmed green (32s).

- [x] **Step 7: Commit**

```bash
git add src/search.cpp tests/test_search.cpp
git commit -m "$(cat <<'EOF'
perf: scale LMR's reduction by depth and move index via a log table

Replaces the flat 1-ply late move reduction with a depth x move_index
log-based table (r = 0.75 + log(depth)*log(move_index)/2.25, floored
at 1), matching today's behavior at LMR's own minimum eligibility
point (depth=3, move_index=4) and reducing more aggressively as
either grows. The probe/re-search structure and do_lmr's eligibility
condition are unchanged.
EOF
)"
```

---

### Task 2: Validate with SPRT

**Files:** none (measurement only; may produce a follow-up commit if the constants
need retuning, or a revert commit if the change doesn't hold up).

**Interfaces:**
- Consumes: `tools/sprt/run_sprt.sh` (existing script), Task 1's commit as the SPRT
  candidate.
- Produces: nothing further tasks depend on — this is the final gate for the feature.

- [x] **Step 1: Confirm SPRT tooling is set up**

```bash
ls tools/sprt/fastchess tools/sprt/books
```

If either is missing, run the one-time setup first: `tools/sprt/setup.sh`.

- [x] **Step 2: Run the SPRT match**

`HEAD` is Task 1's commit; compare against `master` directly (Task 1 is the only
commit in this change — there is no separate no-op refactor commit to isolate against,
unlike the futility pruning plan):

```bash
tools/sprt/run_sprt.sh master HEAD
```

This can take anywhere from several minutes to a couple of hours depending on
hardware; it's safe to run in the background and check back on the output.

**Actual result (divisor 2.25, commit `70c2553`):** ran the full 8000-game budget
(4000 rounds) without either LLR bound being crossed — `Elo: 1.17 +/- 5.61, nElo: 1.59
+/- 7.61`, `LLR: -0.30 (-10.2%) (-2.94, 2.94) [0.00, 5.00]`. A wash, not "H1 accepted".
Total time: ~9h05m.

- [ ] **Step 3a: If "H1 accepted" — done**

No further action. The Task 1 commit stands as-is; the change is validated and kept.

- [x] **Step 3b: If "H0 accepted" (wash or regression) — retune once**

Edit the two constants in `lmr_reduction()` (from Step 4 of Task 1) to a less
aggressive divisor, e.g.:

```cpp
                int r = static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.5);
```

Rebuild, rerun the full test suite:

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Commit as a follow-up:

```bash
git add src/search.cpp
git commit -m "$(cat <<'EOF'
tune: reduce LMR log-table aggressiveness

First attempt (divisor 2.25) didn't clear SPRT; widening the divisor
trades reduction aggressiveness for safety.
EOF
)"
```

Then repeat Step 2 (`tools/sprt/run_sprt.sh master HEAD`). If this second attempt also
reports "H0 accepted", the change isn't a net gain for this engine — revert both
commits and stop:

```bash
git revert --no-edit HEAD HEAD~1
```
