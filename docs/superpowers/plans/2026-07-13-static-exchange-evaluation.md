# Static Exchange Evaluation (SEE) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a standalone, unit-tested Static Exchange Evaluation function and use it to (1) order captures more accurately than MVV-LVA and (2) prune known-losing captures out of quiescence search.

**Architecture:** A new pure-function module `src/see.hpp`/`src/see.cpp` implements the recursive "swap" SEE algorithm over bitboards, independent of search state. `src/search.cpp` then calls it from two existing places (`order_moves`, `quiescence`) instead of/in addition to today's MVV-LVA logic.

**Tech Stack:** C++23, existing `Position`/`Bitboard`/`attacks::*` APIs, doctest for tests, CMake build.

## Global Constraints

- `see(pos, m)` precondition: `m` is a capture (normal or en passant) and **not** a promotion. Callers must check `flag_of(m) != PROMOTION` before calling it (per the spec's scope cut — promotions are not modeled).
- Piece values used by SEE: `{100, 320, 330, 500, 900, 20000}` for `PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING` (matches the existing `mvv_lva_score` table in `search.cpp` for consistency).
- This is a strength-affecting change per `CLAUDE.md`, not a bugfix: it must be validated with `tools/sprt/run_sprt.sh` against current `master` and only kept if SPRT reports "H1 accepted" (see Task 9).
- Every new test calls `attacks::init()` first and asserts `CHECK(p.set(fen))` before using the position, matching existing test conventions (see `tests/test_eval.cpp`).

---

### Task 1: Wire up the `see` module and prove the simplest case (free capture)

**Files:**
- Create: `src/see.hpp`
- Create: `src/see.cpp`
- Create: `tests/test_see.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `int chess::see(const Position& pos, Move m)` — used by all later tasks and by `search.cpp` in Tasks 7-8.

- [ ] **Step 1: Create the header**

`src/see.hpp`:
```cpp
#pragma once

#include "move.hpp"

namespace chess {

class Position;

// Static Exchange Evaluation: net centipawn material result of playing out
// the full capture sequence on to_sq(m), from the perspective of the side
// making move m, assuming both sides always continue with their least
// valuable attacker, but only when doing so doesn't worsen their own
// result.
// Precondition: m is a capture (normal or en passant) and not a promotion.
int see(const Position& pos, Move m);

} // namespace chess
```

- [ ] **Step 2: Write the failing test**

`tests/test_see.cpp`:
```cpp
#include "doctest.h"
#include "see.hpp"
#include "position.hpp"
#include "attacks.hpp"
#include "move.hpp"
using namespace chess;

TEST_CASE("SEE: undefended capture returns the captured piece's value") {
    attacks::init();
    // White queen d1, Black pawn d7, nothing else attacks d7.
    Position p; CHECK(p.set("7k/3p4/8/8/8/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D1, SQ_D7)) == 100);
}
```

- [ ] **Step 3: Wire the test file into the build (link should fail: no `see.cpp` yet)**

Edit `CMakeLists.txt` line 55 (the `chess_tests` executable sources) to add `tests/test_see.cpp`:
```cmake
add_executable(chess_tests tests/test_main.cpp tests/test_types.cpp tests/test_bitboard.cpp tests/test_attacks.cpp tests/test_position.cpp tests/test_movegen.cpp tests/test_perft.cpp tests/test_eval.cpp tests/test_search.cpp tests/test_zobrist.cpp tests/test_tt.cpp tests/test_uci.cpp tests/test_see.cpp)
```

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```
Expected: link failure, `unresolved external symbol chess::see` (MSVC) or `undefined reference to chess::see` (gcc/clang) — `see.hpp` declares the function but no `.cpp` defines it yet.

- [ ] **Step 4: Implement `see()` (the full algorithm, not a partial stub)**

The recursive swap algorithm can't usefully be built in smaller increments than this — the loop that finds each side's least-valuable attacker and the recursion that lets a side decline to continue are one coherent unit. Later tasks add more tests to verify additional scenarios (losing captures, x-ray attackers, en passant, pruning); this step implements the complete function.

`src/see.cpp`:
```cpp
#include "see.hpp"

#include <algorithm>

#include "attacks.hpp"
#include "bitboard.hpp"
#include "position.hpp"
#include "types.hpp"

namespace chess {

namespace {

constexpr int VALUE[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 20000};

// Every piece of either color that attacks `sq`, given custom occupancy
// `occ` (not necessarily pos.occupied() - the caller may have removed
// pieces to simulate a capture sequence). Sliding-piece attacks are
// recomputed against `occ` each call, so removing a piece in front of a
// bishop/rook/queen correctly reveals that slider as a new attacker.
Bitboard attackers_to(const Position& pos, Square sq, Bitboard occ) {
    Bitboard att = 0;
    att |= pawn_attacks(BLACK, sq) & pos.pieces(WHITE, PAWN);
    att |= pawn_attacks(WHITE, sq) & pos.pieces(BLACK, PAWN);
    att |= knight_attacks(sq) & pos.pieces(KNIGHT);
    att |= king_attacks(sq) & pos.pieces(KING);
    Bitboard diagonal_sliders = pos.pieces(BISHOP) | pos.pieces(QUEEN);
    att |= bishop_attacks(sq, occ) & diagonal_sliders;
    Bitboard straight_sliders = pos.pieces(ROOK) | pos.pieces(QUEEN);
    att |= rook_attacks(sq, occ) & straight_sliders;
    return att & occ;
}

// Resolves the rest of the exchange on `sq` for `side`, given that the
// piece now sitting on `sq` is worth `captured_value` to whoever captures
// it next. Returns the best net gain `side` can achieve by continuing to
// capture, clamped to 0 - `side` can always choose to stop instead, which
// is what lets the recursion correctly decline a losing continuation
// (e.g. not recapturing with a queen if it would just be lost to a
// further defender for nothing).
int exchange(const Position& pos, Bitboard occ, Color side, Square sq, int captured_value) {
    Bitboard side_attackers = attackers_to(pos, sq, occ) & pos.pieces(side);
    if (!side_attackers) return 0;

    PieceType attacker_type = NO_PIECE_TYPE;
    Square attacker_sq = SQ_NONE;
    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard b = side_attackers & pos.pieces(side, static_cast<PieceType>(pt));
        if (b) {
            attacker_type = static_cast<PieceType>(pt);
            attacker_sq = lsb(b);
            break;
        }
    }

    Bitboard next_occ = occ & ~square_bb(attacker_sq);
    int gain = captured_value - exchange(pos, next_occ, Color(side ^ 1), sq, VALUE[attacker_type]);
    return std::max(0, gain);
}

} // namespace

int see(const Position& pos, Move m) {
    Square from = from_sq(m);
    Square to = to_sq(m);
    MoveFlag mf = flag_of(m);

    Bitboard occ = pos.occupied();
    occ &= ~square_bb(from);

    int captured_value;
    if (mf == EN_PASSANT) {
        captured_value = VALUE[PAWN];
        Square captured_sq = make_square(file_of(to), rank_of(from));
        occ &= ~square_bb(captured_sq);
    } else {
        captured_value = VALUE[type_of(pos.piece_on(to))];
    }

    PieceType attacker_type = type_of(pos.piece_on(from));
    Color side = Color(pos.side_to_move() ^ 1);
    return captured_value - exchange(pos, occ, side, to, VALUE[attacker_type]);
}

} // namespace chess
```

Edit `CMakeLists.txt` line 20-31 (`ENGINE_SOURCES`) to add `src/see.cpp`:
```cmake
set(ENGINE_SOURCES
  src/bitboard.cpp
  src/attacks.cpp
  src/position.cpp
  src/movegen.cpp
  src/perft.cpp
  src/eval.cpp
  src/search.cpp
  src/zobrist.cpp
  src/tt.cpp
  src/uci.cpp
  src/see.cpp
)
```

- [ ] **Step 5: Run test to verify it passes**

Run:
```bash
cmake --build build && ./build/chess_tests --test-case="SEE: undefended capture returns the captured piece's value"
```
Expected: PASS (1 assertion, 0 failures).

- [ ] **Step 6: Commit**

```bash
git add src/see.hpp src/see.cpp tests/test_see.cpp CMakeLists.txt
git commit -m "feat: add Static Exchange Evaluation (SEE)"
```

---

### Task 2: Verify a losing capture returns a negative value

**Files:**
- Modify: `tests/test_see.cpp`

**Interfaces:**
- Consumes: `int chess::see(const Position& pos, Move m)` (Task 1).

- [ ] **Step 1: Write the test**

Append to `tests/test_see.cpp`:
```cpp
TEST_CASE("SEE: losing capture returns a negative value when the defender wins the exchange") {
    attacks::init();
    // White queen d1 captures the pawn on d7, but Black's knight on b8
    // (the only defender) recaptures the queen for free afterwards:
    // net = pawn(100) - queen(900) = -800.
    Position p; CHECK(p.set("1n5k/3p4/8/8/8/8/8/K2Q4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D1, SQ_D7)) == -800);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run:
```bash
cmake --build build && ./build/chess_tests --test-case="SEE: losing capture returns a negative value when the defender wins the exchange"
```
Expected: PASS. (Task 1's implementation is the complete algorithm, so this confirms it handles a real recapture correctly rather than introducing new code.)

- [ ] **Step 3: Commit**

```bash
git add tests/test_see.cpp
git commit -m "test: verify SEE identifies a losing capture"
```

---

### Task 3: Verify an even trade nets zero

**Files:**
- Modify: `tests/test_see.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_see.cpp`:
```cpp
TEST_CASE("SEE: even rook trade nets zero") {
    attacks::init();
    // White rook a1 takes the rook on a8; Black's rook on h8 recaptures
    // along rank 8. Rook for rook: net = 0.
    Position p; CHECK(p.set("r6r/7k/8/8/8/8/8/R6K w - - 0 1"));
    CHECK(see(p, make_move(SQ_A1, SQ_A8)) == 0);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run:
```bash
cmake --build build && ./build/chess_tests --test-case="SEE: even rook trade nets zero"
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_see.cpp
git commit -m "test: verify SEE resolves an even trade to zero"
```

---

### Task 4: Verify SEE declines a losing continuation instead of forcing it

**Files:**
- Modify: `tests/test_see.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_see.cpp`:
```cpp
TEST_CASE("SEE: declines a further capture that would lose material (stops the exchange early)") {
    attacks::init();
    // White pawn d4 takes the pawn on e5. Black recaptures with the
    // knight on c6 (regaining the pawn: net so far 0). White *could*
    // recapture the knight with the queen on e1, but Black's rook on e8
    // would then win the queen for a rook - a bad trade for White, who
    // therefore should decline it and stop. Final result: 0, not the
    // -580 a naive "always recapture" implementation would compute.
    Position p; CHECK(p.set("4r2k/8/2n5/4p3/3P4/8/8/K3Q3 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D4, SQ_E5)) == 0);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run:
```bash
cmake --build build && ./build/chess_tests --test-case="SEE: declines a further capture that would lose material (stops the exchange early)"
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_see.cpp
git commit -m "test: verify SEE stops an exchange instead of forcing a losing recapture"
```

---

### Task 5: Verify an x-ray attacker is revealed after the front piece is captured

**Files:**
- Modify: `tests/test_see.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_see.cpp`:
```cpp
TEST_CASE("SEE: reveals an x-ray attacker behind a captured piece") {
    attacks::init();
    // White rook d3 takes the knight on d5 (+320). Black's pawn on c6
    // recaptures the rook (+500 for Black). White's second rook on d1,
    // previously blocked by the d3 rook, is now unblocked and recaptures
    // the pawn (+100 for White). Net for White: 320 - 500 + 100 = -80.
    // An implementation that fails to re-reveal the d1 rook after d3 is
    // removed would instead stop after Black's recapture and return
    // -180, so this distinguishes a working x-ray from a broken one.
    Position p; CHECK(p.set("7k/8/2p5/3n4/8/3R4/8/K2R4 w - - 0 1"));
    CHECK(see(p, make_move(SQ_D3, SQ_D5)) == -80);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run:
```bash
cmake --build build && ./build/chess_tests --test-case="SEE: reveals an x-ray attacker behind a captured piece"
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_see.cpp
git commit -m "test: verify SEE reveals x-ray attackers"
```

---

### Task 6: Verify en passant captures use the actual captured pawn's square

**Files:**
- Modify: `tests/test_see.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_see.cpp`:
```cpp
TEST_CASE("SEE: en passant capture uses the actual captured pawn's square") {
    attacks::init();
    // White pawn e5 captures en passant onto d6, removing Black's pawn
    // actually sitting on d5 (not on d6). No other piece attacks d6, so
    // the result is simply the captured pawn's value.
    Position p; CHECK(p.set("7k/8/8/3pP3/8/8/8/K7 w - d6 0 1"));
    CHECK(see(p, make_move(SQ_E5, SQ_D6, EN_PASSANT)) == 100);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run:
```bash
cmake --build build && ./build/chess_tests --test-case="SEE: en passant capture uses the actual captured pawn's square"
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_see.cpp
git commit -m "test: verify SEE handles en passant's actual captured square"
```

---

### Task 7: Use SEE for capture ordering in `order_moves`

**Files:**
- Modify: `src/search.cpp:102-115` (`order_moves`)

**Interfaces:**
- Consumes: `int chess::see(const Position& pos, Move m)` (Task 1), `MoveFlag`/`flag_of` (already used in `search.cpp`).

- [ ] **Step 1: Add the include**

In `src/search.cpp`, add to the includes at the top (after `#include "eval.hpp"` at line 7):
```cpp
#include "see.hpp"
```

- [ ] **Step 2: Change the capture-ordering branch**

In `order_moves` (`src/search.cpp:102-115`), replace:
```cpp
            if (is_capture(pos, m)) return CAPTURE_BASE + mvv_lva_score(pos, m);
```
with:
```cpp
            if (is_capture(pos, m)) {
                int capture_score = flag_of(m) == PROMOTION ? mvv_lva_score(pos, m) : see(pos, m);
                return CAPTURE_BASE + capture_score;
            }
```

- [ ] **Step 3: Rebuild and run the full test suite**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: all existing tests still pass (this changes move ordering only, not legality or search results in a way any test pins exactly — if a test fails, read its failure message before changing test expectations, since a search-result test relying on incidental move-ordering behavior is a legitimate regression to investigate, not just to silence).

- [ ] **Step 4: Commit**

```bash
git add src/search.cpp
git commit -m "feat: order captures by SEE instead of MVV-LVA"
```

---

### Task 8: Use SEE to prune losing captures in quiescence search

**Files:**
- Modify: `src/search.cpp:172-179` (inside `quiescence`)

**Interfaces:**
- Consumes: `int chess::see(const Position& pos, Move m)` (Task 1, already included via Task 7).

- [ ] **Step 1: Change the capture-filtering branch**

In `quiescence` (`src/search.cpp:172-179`), replace:
```cpp
    MoveList moves;
    if (checked) {
        moves = list;
    } else {
        for (Move m : list)
            if (is_capture(pos, m)) moves.add(m);
    }
```
with:
```cpp
    MoveList moves;
    if (checked) {
        moves = list;
    } else {
        for (Move m : list) {
            if (!is_capture(pos, m)) continue;
            if (flag_of(m) != PROMOTION && see(pos, m) < 0) continue;
            moves.add(m);
        }
    }
```

- [ ] **Step 2: Rebuild and run the full test suite**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: all existing tests still pass.

- [ ] **Step 3: Commit**

```bash
git add src/search.cpp
git commit -m "feat: prune losing captures from quiescence search using SEE"
```

---

### Task 9: SPRT validation

**Files:** none (validation only, no code changes).

- [ ] **Step 1: Run SPRT against master**

Run (first time only, if not already done): `tools/sprt/setup.sh`. Then:
```bash
tools/sprt/run_sprt.sh master
```
This builds both `master` (baseline) and the current working tree (candidate, containing Tasks 1-8) and plays an SPRT match between them.

- [ ] **Step 2: Check the result**

The tool reports either "H1 accepted" (the candidate is a confirmed strength improvement) or a rejection/inconclusive result. Per `CLAUDE.md`, only keep this change if "H1 accepted"; if not, the ordering/pruning commits from Tasks 7-8 must be reverted (Task 1-6's `see()` module and its tests may still be kept on their own if desired, since they're independently correct and useful for a future attempt, but report this decision back rather than assuming — the user may prefer a straight revert of everything).

- [ ] **Step 3: Report the outcome**

Report the SPRT result (accepted/rejected, Elo estimate, number of games) back before considering this feature complete.
