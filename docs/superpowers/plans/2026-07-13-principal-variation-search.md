# Principal Variation Search (PVS) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut node count at fixed search depth by adding null-window
Principal Variation Search probing to `negamax`'s move loop, exploiting
the search's existing move-ordering quality (TT move, SEE-ordered
captures, killers, history).

**Architecture:** Only the first move searched at each `negamax` node
keeps its full `(-beta, -alpha)` window. Every later move first gets a
cheap null-window `(-alpha-1, -alpha)` probe (at LMR's reduced depth
when LMR's existing conditions apply), and only pays for a full-window
re-search when that probe proves inconclusive (beats alpha without
already guaranteeing a cutoff). Confined entirely to
`negamax`'s move loop in `src/search.cpp`; no other file changes.

**Tech Stack:** C++23, CMake, doctest (existing `chess_tests` target),
`tools/sprt/` (fastchess-based SPRT harness, already set up in this
repo).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-13-principal-variation-search-design.md`.
- Per `CLAUDE.md`: this is a search heuristic change, not a bugfix, so
  it must be validated by SPRT (`tools/sprt/run_sprt.sh`) after unit
  tests pass; the change is kept only on "H1 accepted", otherwise
  reverted regardless of how clean the unit tests are.
- No changes to the root move loop in `search_best_move`, to
  `quiescence`, or to any tuning constant (see spec's Non-goals).
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
- Tests: `ctest --test-dir build --output-on-failure`, or filtered via
  `./build/Release/chess_tests.exe --test-case="<name>"` (Windows
  multi-config build; the binary lands under `build/Release/` as
  confirmed by this session's build).

---

### Task 1: PVS null-window probing in `negamax`'s move loop

**Files:**
- Modify: `src/search.cpp:296-322` (the move-loop body inside `negamax`)
- Test: `tests/test_search.cpp` (new `TEST_CASE`, inserted after the
  `"search_best_move reuses a warm caller-supplied table to search
  fewer nodes"` case that currently ends at line 282)

**Interfaces:**
- Consumes: existing `negamax(Position&, int depth, int alpha, int
  beta, int ply, std::uint64_t& nodes, TimeGuard&, TranspositionTable&,
  SearchTables&, std::vector<zobrist::Key>&)` signature — unchanged.
  Existing loop-local variables `move_index` (int, 0 at the first move,
  incremented only when a move doesn't cause a cutoff — see
  `search.cpp:295,339`), `capture` (bool), `mf` (`MoveFlag`),
  `gives_check` (bool), `tt_move` (`Move`), `depth`, `alpha`, `beta` are
  all already in scope at this point in the function.
- Produces: no new public interface — `score` (int), used identically
  by the existing code immediately below (best-move tracking, alpha
  update, beta cutoff, killer/history updates, TT store), is unchanged
  in meaning (score from the current move, from the side-to-move's
  perspective at this node).

- [ ] **Step 1: Write the failing test**

  Open `tests/test_search.cpp`. Find this existing test (ends at line
  282):

  ```cpp
  TEST_CASE("search_best_move reuses a warm caller-supplied table to search fewer nodes") {
      attacks::init();
      zobrist::init();
      Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
      SearchLimits lim; lim.depth = 5;

      TranspositionTable warm(16);
      search_best_move(p, lim, warm);                       // first pass: fills the table
      SearchResult reused = search_best_move(p, lim, warm);  // second pass: same table, same position

      TranspositionTable cold(16);
      SearchResult fresh = search_best_move(p, lim, cold);   // never-seeded table, for comparison

      CHECK(reused.nodes < fresh.nodes);
  }
  ```

  Immediately after its closing `}` (and before the blank line +
  `TEST_CASE("nodes_limit stops the search close to the requested node
  budget")`), insert:

  ```cpp
  TEST_CASE("PVS null-window probing keeps node count well below plain alpha-beta") {
      attacks::init();
      Position p; p.set("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1");
      SearchLimits lim; lim.depth = 6;
      TranspositionTable tt(16);
      SearchResult r = search_best_move(p, lim, tt);
      CHECK(r.nodes < 65000); // current master (no PVS): 70,707 nodes at this depth
  }
  ```

- [ ] **Step 2: Build and run the test to verify it fails**

  ```bash
  cmake --build build --config Release
  ./build/Release/chess_tests.exe --test-case="PVS null-window probing keeps node count well below plain alpha-beta"
  ```

  Expected: FAIL — reported node count is 70,707 (current `master`
  behavior), which is not `< 65000`.

- [ ] **Step 3: Implement the PVS move-loop change**

  In `src/search.cpp`, replace this block (currently lines 305-322):

  ```cpp
          int score;
          // Reduce quiet, non-promoting, non-checking moves searched after the
          // first few (they're least likely to be best, per move ordering).
          // If the reduced search still beats alpha, it wasn't actually a bad
          // move, so re-search at full depth before trusting the score - this
          // re-search is what keeps LMR correctness-preserving: a reduction
          // only ever costs extra nodes, never a wrong answer.
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

  with:

  ```cpp
          int score;
          // Reduce quiet, non-promoting, non-checking moves searched after the
          // first few (they're least likely to be best, per move ordering).
          bool do_lmr = depth >= 3 && move_index >= 4 && !capture &&
                        mf != PROMOTION && !gives_check && m != tt_move;
          int move_search_depth = do_lmr ? depth - 2 : depth - 1;

          if (move_index == 0) {
              // Principal Variation Search: the first move at this node is
              // presumed best by move ordering (TT move / top SEE capture),
              // so it alone is worth searching with the full window.
              score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                                nodes, tg, tt, tables, history);
          } else {
              // Every later move first gets a cheap null-window probe (at
              // LMR's reduced depth when do_lmr applies) whose only job is
              // to prove "not better than alpha". Beating alpha means the
              // probe wasn't conclusive: a reduced-depth probe (do_lmr)
              // always needs a full-depth look before it can be trusted -
              // this is what keeps LMR correctness-preserving, a reduction
              // only ever costs extra nodes, never a wrong answer - while a
              // full-depth probe that lands strictly inside (alpha, beta)
              // needs a real full-window re-search for its exact value. A
              // full-depth probe that already reaches >= beta guarantees a
              // cutoff at the parent regardless of the move's exact value,
              // so no re-search is needed for it.
              score = -negamax(pos, move_search_depth, -alpha - 1, -alpha, ply + 1,
                                nodes, tg, tt, tables, history);
              if (score > alpha && (do_lmr || score < beta))
                  score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                                    nodes, tg, tt, tables, history);
          }
  ```

- [ ] **Step 4: Build and run the full test suite to verify everything passes**

  ```bash
  cmake --build build --config Release
  ctest --test-dir build --output-on-failure
  ```

  Expected: all test cases pass, including the new one from Step 1
  (node count now below 65,000 for that position/depth) and every
  pre-existing case in `tests/test_search.cpp` (mate-finding,
  TT-persisted mate scores, warm-TT node reduction, MultiPV ordering,
  repetition/fifty-move edge cases) and the rest of the suite
  (`test_eval.cpp`, `test_see.cpp`, `test_tt.cpp`, etc.), unaffected by
  this change but run here as the regression safety net described in
  the spec.

- [ ] **Step 5: Commit**

  ```bash
  git add src/search.cpp tests/test_search.cpp
  git commit -m "$(cat <<'EOF'
  perf: add PVS null-window probing to negamax's move loop

  Only the first move at each node gets a full-window search; every
  later move is first refuted with a cheap null-window probe (combined
  with LMR's existing reduced-depth probe where applicable), paying for
  a full-window re-search only when the probe proves inconclusive.
  EOF
  )"
  ```

---

### Task 2: SPRT validation

**Files:** none (measurement/validation only; no source changes).

**Interfaces:**
- Consumes: `tools/sprt/run_sprt.sh <base_ref> [candidate_ref]` (already
  set up in this repo — `tools/sprt/fastchess/fastchess.exe` and
  `tools/sprt/books/8moves_v3.pgn` both present, confirmed this
  session). `base_ref` is the commit before Task 1's change:
  `0edca0c` (the design-doc commit, last commit before this plan's
  implementation work). `candidate_ref` is omitted, so the script
  builds the current working tree (i.e. `master` with Task 1's commit
  applied) as the candidate.
- Produces: a verdict ("H1 accepted" or "H0 accepted") printed to the
  terminal and a PGN file under `tools/sprt/results/`. No code artifact.

- [ ] **Step 1: Run the SPRT match**

  ```bash
  tools/sprt/run_sprt.sh 0edca0c
  ```

  This builds `0edca0c` (baseline, pre-PVS) and the current working
  tree (candidate, with Task 1's commit) into isolated worktrees,
  then runs a fastchess SPRT match between them using the default
  bounds (`elo0=0, elo1=5, alpha=0.05, beta=0.05, tc=10+0.1`). Takes
  several minutes depending on core count.

- [ ] **Step 2: Interpret the result**

  Read the final lines of output.

  - If **"H1 accepted"**: PVS is a confirmed strength improvement.
    Task 1's commit stays as-is. Nothing further to do — the feature
    is complete.
  - If **"H0 accepted"** (no gain / regression) or the match is
    inconclusive after the round budget: the change must be reverted
    per `CLAUDE.md`, regardless of the unit tests passing cleanly.
    Run:

    ```bash
    git revert --no-edit <task-1-commit-hash>
    ```

    (substitute the actual commit hash produced by Task 1, Step 5),
    then stop — do not re-attempt PVS in this plan; any retry needs a
    new design addressing why the SPRT result came back negative
    (e.g. a bug in the re-search condition silently narrowing the
    effective search rather than just pruning redundant work).
