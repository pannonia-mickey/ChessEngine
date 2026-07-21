# Pawn Structure Evaluation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add isolated/doubled/backward pawn penalties to the evaluation (tapered MG/EG), consolidate the existing passed-pawn bonus into the same computation, and cache the combined result behind a pawn hash table keyed on a new incrementally-maintained `Position::pawn_key()` — validated by SPRT per `CLAUDE.md`.

**Architecture:** A new `Position::pawn_key_` (incrementally XORed in `put_piece`/`remove_piece`, pawns only) plus new files `src/pawn_eval.hpp`/`.cpp` holding the pawn-only scoring logic and a small, never-cleared pawn hash table. `eval.cpp` drops its own passed-pawn code and calls the new `pawn_structure()` once. No other files change.

**Tech Stack:** C++23, CMake + doctest (`chess_tests` target), fastchess-based SPRT tooling in `tools/sprt/`.

## Global Constraints

- C++23, CMake ≥ 3.20 (already configured; the only build-system change is registering `src/pawn_eval.cpp` in `CMakeLists.txt`'s `ENGINE_SOURCES`).
- Warnings must stay clean: `/W4` on MSVC, `-Wall -Wextra` otherwise.
- Build/test commands: `cmake --build build --config Release`, then `./build/Release/chess_tests.exe` (or `ctest --test-dir build --output-on-failure`).
- Per `CLAUDE.md`, this changes evaluation output (a search decision, not a bugfix): it may only be kept if `tools/sprt/run_sprt.sh` reports **"H1 accepted"**.
- New pawn-eval logic lives entirely in new files `src/pawn_eval.hpp`/`src/pawn_eval.cpp` — no new test files; new tests are appended to the existing `tests/test_eval.cpp` (pawn structure + hash) and `tests/test_zobrist.cpp` (`pawn_key()`).
- Source: `docs/superpowers/specs/2026-07-21-pawn-structure-eval-design.md`.

---

### Task 1: Incremental `Position::pawn_key()`

**Files:**
- Modify: `src/position.hpp:60` (add accessor after `key()`), `src/position.hpp:105` (add member after `key_`)
- Modify: `src/position.cpp:11-32` (`put_piece`/`remove_piece`), `src/position.cpp:129-132` (`Position::set()` reset block)
- Test: `tests/test_zobrist.cpp` (append new `TEST_CASE`s)

**Interfaces:**
- Consumes: `zobrist::psq[Piece][Square]` (existing, `src/zobrist.hpp`), `type_of(Piece)` (existing, `src/types.hpp`).
- Produces: `Position::pawn_key() const -> zobrist::Key` — a Zobrist key of just the pawns, used by Task 2's `pawn_eval.cpp`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_zobrist.cpp` (add `#include "movegen.hpp"` and `#include "bitboard.hpp"` to the top of the file alongside the existing includes):

```cpp
TEST_CASE("pawn_key changes on a pawn move but not on a non-pawn move") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    zobrist::Key before = p.pawn_key();
    StateInfo st;

    Move pawn_move = make_move(SQ_E2, SQ_E4);
    p.do_move(pawn_move, st);
    CHECK(p.pawn_key() != before);
    p.undo_move(pawn_move, st);
    CHECK(p.pawn_key() == before);

    Move knight_move = make_move(SQ_G1, SQ_F3);
    p.do_move(knight_move, st);
    CHECK(p.pawn_key() == before); // knight move must not touch pawn_key_
    p.undo_move(knight_move, st);
}

TEST_CASE("pawn_key matches a from-scratch recompute after a pawn capture") {
    attacks::init();
    zobrist::init();
    Position p; p.set("rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 1");
    StateInfo st;
    p.do_move(make_move(SQ_D4, SQ_E5), st); // pawn captures pawn
    zobrist::Key incremental = p.pawn_key();

    Position fresh; fresh.set(p.fen());
    CHECK(fresh.pawn_key() == incremental);
}

namespace {
zobrist::Key reference_pawn_key(const Position& pos) {
    zobrist::Key k = 0;
    for (int c = 0; c < COLOR_NB; ++c) {
        Bitboard pawns = pos.pieces(Color(c), PAWN);
        while (pawns) {
            Square s = pop_lsb(pawns);
            k ^= zobrist::psq[make_piece(Color(c), PAWN)][s];
        }
    }
    return k;
}

void walk_and_check_pawn_key(Position& pos, int depth) {
    CHECK(pos.pawn_key() == reference_pawn_key(pos));
    if (depth == 0) return;
    MoveList moves;
    generate_legal(pos, moves);
    StateInfo st;
    for (Move m : moves) {
        pos.do_move(m, st);
        walk_and_check_pawn_key(pos, depth - 1);
        pos.undo_move(m, st);
    }
}
} // namespace

TEST_CASE("pawn_key matches a from-scratch reference across a do/undo tree") {
    attacks::init();
    zobrist::init();
    Position p; p.set("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    walk_and_check_pawn_key(p, 3);
}
```

- [ ] **Step 2: Build to verify it fails to compile**

```bash
cmake --build build --config Release
```

Expected: FAIL — `Position::pawn_key()` doesn't exist yet (compile error).

- [ ] **Step 3: Add the `pawn_key_` member and accessor**

In `src/position.hpp`, find:

```cpp
    zobrist::Key key() const { return key_; }

    // Incrementally-maintained material+PST sums (White minus Black, before
```

Replace with:

```cpp
    zobrist::Key key() const { return key_; }

    // Zobrist key of just the pawns (same zobrist::psq table as key(), but
    // XORed only for PAWN pieces), kept incrementally up to date by
    // put_piece()/remove_piece() alongside key_. A pawn structure's
    // isolated/doubled/backward/passed-pawn score never depends on any
    // non-pawn piece, so this key lets src/pawn_eval.cpp's pawn-structure
    // scoring cache results across positions that share the same pawn
    // layout.
    zobrist::Key pawn_key() const { return pawn_key_; }

    // Incrementally-maintained material+PST sums (White minus Black, before
```

Then find:

```cpp
    zobrist::Key key_ = 0;
    int mg_psq_ = 0;
```

Replace with:

```cpp
    zobrist::Key key_ = 0;
    zobrist::Key pawn_key_ = 0;
    int mg_psq_ = 0;
```

- [ ] **Step 4: Update `put_piece`/`remove_piece` and `Position::set()` in `src/position.cpp`**

Find:

```cpp
void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    by_type_[type_of(p)] |= square_bb(s);
    by_color_[color_of(p)] |= square_bb(s);
    key_ ^= zobrist::psq[p][s];

    mg_psq_ += PSQ_MG[p][s];
    eg_psq_ += PSQ_EG[p][s];
    phase_ += PHASE_WEIGHT[type_of(p)];
}

void Position::remove_piece(Square s) {
    Piece p = board_[s];
    by_type_[type_of(p)] &= ~square_bb(s);
    by_color_[color_of(p)] &= ~square_bb(s);
    board_[s] = NO_PIECE;
    key_ ^= zobrist::psq[p][s];

    mg_psq_ -= PSQ_MG[p][s];
    eg_psq_ -= PSQ_EG[p][s];
    phase_ -= PHASE_WEIGHT[type_of(p)];
}
```

Replace with:

```cpp
void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    by_type_[type_of(p)] |= square_bb(s);
    by_color_[color_of(p)] |= square_bb(s);
    key_ ^= zobrist::psq[p][s];
    if (type_of(p) == PAWN) pawn_key_ ^= zobrist::psq[p][s];

    mg_psq_ += PSQ_MG[p][s];
    eg_psq_ += PSQ_EG[p][s];
    phase_ += PHASE_WEIGHT[type_of(p)];
}

void Position::remove_piece(Square s) {
    Piece p = board_[s];
    by_type_[type_of(p)] &= ~square_bb(s);
    by_color_[color_of(p)] &= ~square_bb(s);
    board_[s] = NO_PIECE;
    key_ ^= zobrist::psq[p][s];
    if (type_of(p) == PAWN) pawn_key_ ^= zobrist::psq[p][s];

    mg_psq_ -= PSQ_MG[p][s];
    eg_psq_ -= PSQ_EG[p][s];
    phase_ -= PHASE_WEIGHT[type_of(p)];
}
```

Then find (inside `Position::set()`):

```cpp
    key_ = 0;
    mg_psq_ = 0;
    eg_psq_ = 0;
    phase_ = 0;
```

Replace with:

```cpp
    key_ = 0;
    pawn_key_ = 0;
    mg_psq_ = 0;
    eg_psq_ = 0;
    phase_ = 0;
```

- [ ] **Step 5: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — all new tests plus every pre-existing test (this change is additive; `key_` behavior is untouched).

- [ ] **Step 6: Commit**

```bash
git add src/position.hpp src/position.cpp tests/test_zobrist.cpp
git commit -m "$(cat <<'EOF'
feat: track an incremental pawn-only Zobrist key on Position

Position::pawn_key() XORs the same zobrist::psq table as key(), but
only for pawns, kept up to date in put_piece()/remove_piece()'s
existing choke point. Lets a future pawn-structure eval cache results
by pawn layout alone, independent of every other piece on the board.
EOF
)"
```

---

### Task 2: `pawn_eval.hpp`/`.cpp` — isolated pawns + consolidated passed-pawn bonus

**Files:**
- Create: `src/pawn_eval.hpp`, `src/pawn_eval.cpp`
- Modify: `CMakeLists.txt:67-78` (add `src/pawn_eval.cpp` to `ENGINE_SOURCES`)
- Modify: `src/eval.cpp` (remove `is_passed_pawn`/`passed_pawn_bonus`, call the new `pawn_structure()`)
- Test: `tests/test_eval.cpp` (append new `TEST_CASE`)

**Interfaces:**
- Consumes: `Position::pieces(Color, PieceType)` (existing), `Bitboard`/`FILE_A_BB`..`FILE_H_BB`/`popcount`/`pop_lsb`/`square_bb` (`src/bitboard.hpp`), `file_of`/`rank_of`/`make_square` (`src/types.hpp`).
- Produces: `void pawn_structure(const Position& pos, int& mg, int& eg)` (`src/pawn_eval.hpp`) — White-minus-Black, MG/EG. Task 3 and Task 4 extend its internals; Task 5 wraps it in a cache. `eval.cpp` and later tasks' tests call it by this exact name and signature.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_eval.cpp` (add `#include "pawn_eval.hpp"` to the top alongside the existing includes):

```cpp
TEST_CASE("an isolated pawn scores worse than one with adjacent-file support") {
    attacks::init();
    // Both positions: White Ka1, pawns d4 + (b2 or c2), e2; Black Ka8,
    // pawns b5, c5 (block b2/c2/d4 from ever being passed, on whichever
    // file the moving pawn sits on). e2 supports d4 in both (so d4's own
    // isolated/passed/backward status never changes) and is unopposed in
    // both, so its own passed bonus is identical in both and cancels in
    // the comparison below.
    //
    // "isolated": White's second pawn is on b2 - neighbor files a/c have
    // no White pawn, so b2 is isolated. d4's own isolated status is
    // unaffected (still supported by e2).
    //
    // "supported": the same pawn sits on c2 instead - directly adjacent
    // to d4, so neither c2 nor d4 is isolated. b2 and c2 share the same
    // EG_PST value at this rank (row 6, files b/c both = 8), so this
    // isn't a PST artifact - the isolated penalty is what decides it.
    Position isolated;  CHECK(isolated.set("k7/8/8/1pp5/3P4/8/1P2P3/K7 w - - 0 1"));
    Position supported; CHECK(supported.set("k7/8/8/1pp5/3P4/8/2P1P3/K7 w - - 0 1"));
    CHECK(evaluate(isolated) < evaluate(supported));
}
```

- [ ] **Step 2: Build to verify it fails to compile**

```bash
cmake --build build --config Release
```

Expected: FAIL — `pawn_eval.hpp` doesn't exist yet.

- [ ] **Step 3: Create `src/pawn_eval.hpp`**

```cpp
#pragma once

namespace chess {

class Position;

// Isolated pawn penalty and the passed-pawn bonus, tapered MG/EG, White
// minus Black. (Doubled and backward penalties are added by later
// tasks; a pawn hash cache is added by a later task too - this first
// version always computes from scratch.)
void pawn_structure(const Position& pos, int& mg, int& eg);

} // namespace chess
```

- [ ] **Step 4: Create `src/pawn_eval.cpp`**

```cpp
#include "pawn_eval.hpp"

#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr Bitboard FILE_BB[8] = {
    FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
    FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
};

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

// Flat penalty for a pawn with no same-color pawn on either adjacent
// file, at any rank - it can never be defended by a pawn advance.
constexpr int ISOLATED_MG = -12;
constexpr int ISOLATED_EG = -18;

// Detect whether a pawn is passed: no enemy pawns on the pawn's file or
// adjacent files ahead of it (in the direction of advancement).
bool is_passed_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s), r = rank_of(s);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    for (int nf = std::max(0, f - 1); nf <= std::min(7, f + 1); ++nf) {
        for (int nr = 0; nr < 8; ++nr) {
            bool ahead = (c == WHITE) ? (nr > r) : (nr < r);
            if (!ahead) continue;
            if (enemy_pawns & square_bb(make_square(nf, nr))) return false;
        }
    }
    return true;
}

bool is_isolated_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard neighbor_files = 0;
    if (f > 0) neighbor_files |= FILE_BB[f - 1];
    if (f < 7) neighbor_files |= FILE_BB[f + 1];
    return (own_pawns & neighbor_files) == 0;
}

void pawn_structure_for_color(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard scan = pawns;
    while (scan) {
        Square s = pop_lsb(scan);
        if (is_passed_pawn(pos, c, s)) {
            int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            mg += PASSED_PAWN_MG[rel_rank];
            eg += PASSED_PAWN_EG[rel_rank];
        }
        if (is_isolated_pawn(pos, c, s)) {
            mg += ISOLATED_MG;
            eg += ISOLATED_EG;
        }
    }
}

} // namespace

void pawn_structure(const Position& pos, int& mg, int& eg) {
    int mg_w, eg_w, mg_b, eg_b;
    pawn_structure_for_color(pos, WHITE, mg_w, eg_w);
    pawn_structure_for_color(pos, BLACK, mg_b, eg_b);
    mg = mg_w - mg_b;
    eg = eg_w - eg_b;
}

} // namespace chess
```

- [ ] **Step 5: Register the new source file in `CMakeLists.txt`**

Find:

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
  src/bench.cpp
)
```

Replace with:

```cmake
set(ENGINE_SOURCES
  src/bitboard.cpp
  src/attacks.cpp
  src/position.cpp
  src/movegen.cpp
  src/perft.cpp
  src/eval.cpp
  src/pawn_eval.cpp
  src/search.cpp
  src/zobrist.cpp
  src/tt.cpp
  src/uci.cpp
  src/see.cpp
  src/bench.cpp
)
```

- [ ] **Step 6: Wire `pawn_structure()` into `eval.cpp`, removing the old passed-pawn code**

In `src/eval.cpp`, find the include block:

```cpp
#include "eval.hpp"
#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"
#include "eval_tables.hpp"

#include <algorithm>
```

Replace with:

```cpp
#include "eval.hpp"
#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"
#include "eval_tables.hpp"
#include "pawn_eval.hpp"

#include <algorithm>
```

Find (the whole `is_passed_pawn`/PST-constants/`passed_pawn_bonus` block):

```cpp
// Detect whether a pawn is passed: no enemy pawns on the pawn's file or
// adjacent files ahead of it (in the direction of advancement).
bool is_passed_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s), r = rank_of(s);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    for (int nf = std::max(0, f - 1); nf <= std::min(7, f + 1); ++nf) {
        for (int nr = 0; nr < 8; ++nr) {
            bool ahead = (c == WHITE) ? (nr > r) : (nr < r);
            if (!ahead) continue;
            if (enemy_pawns & square_bb(make_square(nf, nr))) return false;
        }
    }
    return true;
}

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

void passed_pawn_bonus(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    while (pawns) {
        Square s = pop_lsb(pawns);
        if (!is_passed_pawn(pos, c, s)) continue;
        int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
        mg += PASSED_PAWN_MG[rel_rank];
        eg += PASSED_PAWN_EG[rel_rank];
    }
}

// Bonus per own pawn found on the two ranks directly in front of the king,
```

Replace with (deleting the moved block, keeping the king-shield comment/code that followed it):

```cpp
// Bonus per own pawn found on the two ranks directly in front of the king,
```

Find (inside `evaluate()`):

```cpp
    int mg_score = pos.mg_psq();
    int eg_score = pos.eg_psq();

    int pp_mg_w, pp_eg_w, pp_mg_b, pp_eg_b;
    passed_pawn_bonus(pos, WHITE, pp_mg_w, pp_eg_w);
    passed_pawn_bonus(pos, BLACK, pp_mg_b, pp_eg_b);
    mg_score += pp_mg_w - pp_mg_b;
    eg_score += pp_eg_w - pp_eg_b;
    mg_score += king_safety(pos, WHITE) - king_safety(pos, BLACK);
```

Replace with:

```cpp
    int mg_score = pos.mg_psq();
    int eg_score = pos.eg_psq();

    int pawn_mg, pawn_eg;
    pawn_structure(pos, pawn_mg, pawn_eg);
    mg_score += pawn_mg;
    eg_score += pawn_eg;
    mg_score += king_safety(pos, WHITE) - king_safety(pos, BLACK);
```

- [ ] **Step 7: Reconfigure, build, and run the full test suite**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — the new isolated-pawn test, and every pre-existing test including all of `test_eval.cpp`'s passed-pawn tests (this refactor must not change `evaluate()`'s output for any position that doesn't have an isolated pawn).

- [ ] **Step 8: Commit**

```bash
git add src/pawn_eval.hpp src/pawn_eval.cpp src/eval.cpp CMakeLists.txt tests/test_eval.cpp
git commit -m "$(cat <<'EOF'
feat: add isolated pawn penalty, consolidate passed-pawn scoring

Moves the existing passed-pawn bonus out of eval.cpp into a new
src/pawn_eval.cpp alongside a new isolated-pawn penalty - both are
pure functions of pawn placement, so they belong together ahead of a
later pawn-hash cache. evaluate()'s output is unchanged for any
position without an isolated pawn.
EOF
)"
```

---

### Task 3: Doubled pawn penalty

**Files:**
- Modify: `src/pawn_eval.cpp` (replace entire file contents)
- Test: `tests/test_eval.cpp` (append new `TEST_CASE`)

**Interfaces:**
- Consumes: same as Task 2.
- Produces: no new public symbols — `pawn_structure()`'s signature and behavior for non-doubled positions are unchanged; later tasks build on this same file.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_eval.cpp`:

```cpp
TEST_CASE("doubled pawns score worse than the same pawn count spread across files") {
    attacks::init();
    // Both positions: White Ka1, pawns c2, d2, and a third pawn; Black
    // Ka8, pawns c6, d6, e6 (block c2/d2/d4/e4 from ever being passed,
    // and give c2/d2 adjacent-file support so isolation never enters the
    // comparison - see the FEN comments below for the isolation check).
    //
    // "doubled": the third pawn is on d4 - same file as d2, so the
    // d-file has 2 pawns (1 extra -> one doubled penalty application).
    // "spread": the third pawn is on e4 instead - c2/d2/e4 are each on
    // their own file, so no doubled penalty anywhere. d4 and e4 share
    // the same EG_PST value at this rank (row 4, files d/e both = -7),
    // so this isn't a PST artifact.
    //
    // Neither c2 nor d2 is ever isolated in either position (they
    // support each other, file-adjacent); the third pawn (d4 or e4) is
    // supported by c2/d2 in both cases too (d and e are both adjacent
    // to d2's d-file... e4's neighbor is d, which has d2) - so isolation
    // doesn't differ between the two positions either.
    Position doubled; CHECK(doubled.set("k7/8/2ppp3/8/3P4/8/2PP4/K7 w - - 0 1"));
    Position spread;  CHECK(spread.set("k7/8/2ppp3/8/4P3/8/2PP4/K7 w - - 0 1"));
    CHECK(evaluate(doubled) < evaluate(spread));
}
```

- [ ] **Step 2: Build and run the new test to verify it fails**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="doubled pawns score worse than the same pawn count spread across files"
```

Expected: FAIL — no doubled penalty exists yet, so both positions score equally (modulo the identical-both PST/passed terms).

- [ ] **Step 3: Replace `src/pawn_eval.cpp` with the doubled-penalty version**

Replace the entire contents of `src/pawn_eval.cpp` with:

```cpp
#include "pawn_eval.hpp"

#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr Bitboard FILE_BB[8] = {
    FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
    FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
};

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

// Flat penalty for a pawn with no same-color pawn on either adjacent
// file, at any rank - it can never be defended by a pawn advance.
constexpr int ISOLATED_MG = -12;
constexpr int ISOLATED_EG = -18;

// Penalty applied once per extra pawn beyond the first on a file (a
// 3-pawn stack applies this twice, not once and not three times).
constexpr int DOUBLED_MG = -8;
constexpr int DOUBLED_EG = -16;

// Detect whether a pawn is passed: no enemy pawns on the pawn's file or
// adjacent files ahead of it (in the direction of advancement).
bool is_passed_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s), r = rank_of(s);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    for (int nf = std::max(0, f - 1); nf <= std::min(7, f + 1); ++nf) {
        for (int nr = 0; nr < 8; ++nr) {
            bool ahead = (c == WHITE) ? (nr > r) : (nr < r);
            if (!ahead) continue;
            if (enemy_pawns & square_bb(make_square(nf, nr))) return false;
        }
    }
    return true;
}

bool is_isolated_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard neighbor_files = 0;
    if (f > 0) neighbor_files |= FILE_BB[f - 1];
    if (f < 7) neighbor_files |= FILE_BB[f + 1];
    return (own_pawns & neighbor_files) == 0;
}

void pawn_structure_for_color(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard scan = pawns;
    while (scan) {
        Square s = pop_lsb(scan);
        if (is_passed_pawn(pos, c, s)) {
            int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            mg += PASSED_PAWN_MG[rel_rank];
            eg += PASSED_PAWN_EG[rel_rank];
        }
        if (is_isolated_pawn(pos, c, s)) {
            mg += ISOLATED_MG;
            eg += ISOLATED_EG;
        }
    }
    for (int f = 0; f < 8; ++f) {
        int count = popcount(pawns & FILE_BB[f]);
        if (count > 1) {
            mg += DOUBLED_MG * (count - 1);
            eg += DOUBLED_EG * (count - 1);
        }
    }
}

} // namespace

void pawn_structure(const Position& pos, int& mg, int& eg) {
    int mg_w, eg_w, mg_b, eg_b;
    pawn_structure_for_color(pos, WHITE, mg_w, eg_w);
    pawn_structure_for_color(pos, BLACK, mg_b, eg_b);
    mg = mg_w - mg_b;
    eg = eg_w - eg_b;
}

} // namespace chess
```

- [ ] **Step 4: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — the new doubled-pawn test and every pre-existing test.

- [ ] **Step 5: Commit**

```bash
git add src/pawn_eval.cpp tests/test_eval.cpp
git commit -m "$(cat <<'EOF'
feat: add doubled pawn penalty

Applied once per extra pawn beyond the first on a file, computed
per-file rather than per-pawn to avoid double-penalizing a stack.
EOF
)"
```

---

### Task 4: Backward pawn penalty

**Files:**
- Modify: `src/pawn_eval.cpp` (replace entire file contents)
- Test: `tests/test_eval.cpp` (append new `TEST_CASE`)

**Interfaces:**
- Consumes: same as Task 2, plus `pawn_attacks(Color, Square)` (`src/attacks.hpp`, already used by `Position::square_attacked_by()` for the same reverse-trick pattern).
- Produces: no new public symbols.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_eval.cpp`:

```cpp
TEST_CASE("a backward pawn whose stop square is enemy-pawn-controlled scores worse than one that isn't") {
    attacks::init();
    // Both positions: White Ka1, pawns c6, e6, d2; Black Ka8, pawns f5
    // (supports whichever e-file square Black's other pawn sits on, so
    // that pawn is never itself flagged backward or isolated - keeping
    // this test isolated to White's d2), and a second pawn on e4 or e5.
    //
    // "backward": Black's second pawn is on e4, which attacks d3 (d2's
    // stop square) - d2 has no c/e-file White neighbor at its rank or
    // behind (c6/e6 are far more advanced), and isn't passed (Black's
    // e-pawn blocks the e-file), so all three backward conditions hold.
    //
    // "advanced": Black's second pawn is on e5 instead - e5 attacks d4
    // and f4, not d3, so d2's stop square is safe and d2 is not
    // backward (still not passed either, still blocked by the e-file
    // pawn).
    Position backward; CHECK(backward.set("k7/8/2P1P3/5p2/4p3/8/3P4/K7 w - - 0 1"));
    Position advanced; CHECK(advanced.set("k7/8/2P1P3/4pp2/8/8/3P4/K7 w - - 0 1"));
    CHECK(evaluate(backward) < evaluate(advanced));
}
```

- [ ] **Step 2: Build and run the new test to verify it fails**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe --test-case="a backward pawn whose stop square is enemy-pawn-controlled scores worse than one that isn't"
```

Expected: FAIL — no backward penalty exists yet.

- [ ] **Step 3: Replace `src/pawn_eval.cpp` with the backward-penalty version**

Replace the entire contents of `src/pawn_eval.cpp` with:

```cpp
#include "pawn_eval.hpp"

#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr Bitboard FILE_BB[8] = {
    FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
    FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
};

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

// Flat penalty for a pawn with no same-color pawn on either adjacent
// file, at any rank - it can never be defended by a pawn advance.
constexpr int ISOLATED_MG = -12;
constexpr int ISOLATED_EG = -18;

// Penalty applied once per extra pawn beyond the first on a file (a
// 3-pawn stack applies this twice, not once and not three times).
constexpr int DOUBLED_MG = -8;
constexpr int DOUBLED_EG = -16;

// Penalty for a pawn that (a) isn't passed, (b) has no same-color pawn
// on an adjacent file positioned to ever support it (i.e. at the same
// or a less advanced rank), and (c) can't safely advance because an
// enemy pawn controls the square directly ahead.
constexpr int BACKWARD_MG = -8;
constexpr int BACKWARD_EG = -12;

// Detect whether a pawn is passed: no enemy pawns on the pawn's file or
// adjacent files ahead of it (in the direction of advancement).
bool is_passed_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s), r = rank_of(s);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    for (int nf = std::max(0, f - 1); nf <= std::min(7, f + 1); ++nf) {
        for (int nr = 0; nr < 8; ++nr) {
            bool ahead = (c == WHITE) ? (nr > r) : (nr < r);
            if (!ahead) continue;
            if (enemy_pawns & square_bb(make_square(nf, nr))) return false;
        }
    }
    return true;
}

bool is_isolated_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard neighbor_files = 0;
    if (f > 0) neighbor_files |= FILE_BB[f - 1];
    if (f < 7) neighbor_files |= FILE_BB[f + 1];
    return (own_pawns & neighbor_files) == 0;
}

// A pawn of color `by` attacks square `s`? Reverse trick matching
// Position::square_attacked_by()'s pattern (src/position.cpp), but
// restricted to pawns only - the backward-pawn definition below
// specifically means "an enemy pawn controls the stop square", not
// "any enemy piece".
bool pawn_attacks_square(const Position& pos, Square s, Color by) {
    Color opponent = Color(by ^ 1);
    return (pawn_attacks(opponent, s) & pos.pieces(by, PAWN)) != 0;
}

// Precondition: `s` is not a passed pawn (the caller checks this first,
// so this doesn't redundantly recompute is_passed_pawn()).
bool is_backward_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    int rel_r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);

    for (int nf = f - 1; nf <= f + 1; nf += 2) {
        if (nf < 0 || nf > 7) continue;
        Bitboard neighbors = own_pawns & FILE_BB[nf];
        while (neighbors) {
            Square ns = pop_lsb(neighbors);
            int neighbor_rel_r = (c == WHITE) ? rank_of(ns) : 7 - rank_of(ns);
            if (neighbor_rel_r <= rel_r) return false;
        }
    }

    Color them = Color(c ^ 1);
    Square stop_sq = make_square(f, rank_of(s) + (c == WHITE ? 1 : -1));
    return pawn_attacks_square(pos, stop_sq, them);
}

void pawn_structure_for_color(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard scan = pawns;
    while (scan) {
        Square s = pop_lsb(scan);
        bool passed = is_passed_pawn(pos, c, s);
        if (passed) {
            int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            mg += PASSED_PAWN_MG[rel_rank];
            eg += PASSED_PAWN_EG[rel_rank];
        } else if (is_backward_pawn(pos, c, s)) {
            mg += BACKWARD_MG;
            eg += BACKWARD_EG;
        }
        if (is_isolated_pawn(pos, c, s)) {
            mg += ISOLATED_MG;
            eg += ISOLATED_EG;
        }
    }
    for (int f = 0; f < 8; ++f) {
        int count = popcount(pawns & FILE_BB[f]);
        if (count > 1) {
            mg += DOUBLED_MG * (count - 1);
            eg += DOUBLED_EG * (count - 1);
        }
    }
}

} // namespace

void pawn_structure(const Position& pos, int& mg, int& eg) {
    int mg_w, eg_w, mg_b, eg_b;
    pawn_structure_for_color(pos, WHITE, mg_w, eg_w);
    pawn_structure_for_color(pos, BLACK, mg_b, eg_b);
    mg = mg_w - mg_b;
    eg = eg_w - eg_b;
}

} // namespace chess
```

- [ ] **Step 4: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — the new backward-pawn test and every pre-existing test.

- [ ] **Step 5: Commit**

```bash
git add src/pawn_eval.cpp tests/test_eval.cpp
git commit -m "$(cat <<'EOF'
feat: add backward pawn penalty

A pawn counts as backward when it isn't passed, has no same-color
pawn on an adjacent file able to ever support it (at the same or a
less advanced rank), and its stop square is enemy-pawn-controlled.
EOF
)"
```

---

### Task 5: Pawn hash cache

**Files:**
- Modify: `src/pawn_eval.hpp` (add `pawn_structure_uncached()` declaration)
- Modify: `src/pawn_eval.cpp` (replace entire file contents)
- Test: `tests/test_eval.cpp` (append new `TEST_CASE`)

**Interfaces:**
- Consumes: `Position::pawn_key()` (Task 1).
- Produces: `void pawn_structure_uncached(const Position& pos, int& mg, int& eg)` — same computation as `pawn_structure()` but always bypasses the cache; exposed only for the consistency test below. `pawn_structure()`'s signature and observable behavior are unchanged for any caller outside this test.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_eval.cpp`:

```cpp
namespace {
void check_pawn_structure_matches_uncached(Position& pos) {
    int cached_mg, cached_eg, uncached_mg, uncached_eg;
    pawn_structure(pos, cached_mg, cached_eg);
    pawn_structure_uncached(pos, uncached_mg, uncached_eg);
    CHECK(cached_mg == uncached_mg);
    CHECK(cached_eg == uncached_eg);
}

void walk_and_check_pawn_structure(Position& pos, int depth) {
    check_pawn_structure_matches_uncached(pos);
    if (depth == 0) return;
    MoveList moves;
    generate_legal(pos, moves);
    StateInfo st;
    for (Move m : moves) {
        pos.do_move(m, st);
        walk_and_check_pawn_structure(pos, depth - 1);
        pos.undo_move(m, st);
    }
}
} // namespace

TEST_CASE("cached pawn_structure matches the uncached computation across a do/undo tree") {
    attacks::init();
    struct { const char* fen; int depth; } cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
    };
    for (auto& c : cases) {
        Position p;
        p.set(c.fen);
        walk_and_check_pawn_structure(p, c.depth);
    }
}
```

- [ ] **Step 2: Build to verify it fails to compile**

```bash
cmake --build build --config Release
```

Expected: FAIL — `pawn_structure_uncached` doesn't exist yet.

- [ ] **Step 3: Add the declaration to `src/pawn_eval.hpp`**

Replace the entire contents of `src/pawn_eval.hpp` with:

```cpp
#pragma once

namespace chess {

class Position;

// Isolated + doubled + backward penalties and the passed-pawn bonus,
// tapered MG/EG, White minus Black. Backed by a process-lifetime pawn
// hash cache keyed on pos.pawn_key() - never needs clearing, since a
// given pawn structure scores identically in any position/game.
void pawn_structure(const Position& pos, int& mg, int& eg);

// Same computation as pawn_structure(), but always recomputed from
// scratch, bypassing the pawn hash cache. Exposed only so tests can
// cross-check the cached and uncached paths against each other (see
// tests/test_eval.cpp's pawn hash consistency test) - not meant to be
// called from eval.cpp.
void pawn_structure_uncached(const Position& pos, int& mg, int& eg);

} // namespace chess
```

- [ ] **Step 4: Replace `src/pawn_eval.cpp` with the hash-cached version**

Replace the entire contents of `src/pawn_eval.cpp` with:

```cpp
#include "pawn_eval.hpp"

#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"
#include "attacks.hpp"
#include "zobrist.hpp"

#include <algorithm>
#include <vector>

namespace chess {

namespace {

constexpr Bitboard FILE_BB[8] = {
    FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
    FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
};

// Bonus by rank (own-perspective: 0 = own back rank, 7 = promotion rank);
// larger in the endgame, since passed pawns matter most once material
// thins out and there's less to stop them.
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 35, 60, 100, 150, 0};

// Flat penalty for a pawn with no same-color pawn on either adjacent
// file, at any rank - it can never be defended by a pawn advance.
constexpr int ISOLATED_MG = -12;
constexpr int ISOLATED_EG = -18;

// Penalty applied once per extra pawn beyond the first on a file (a
// 3-pawn stack applies this twice, not once and not three times).
constexpr int DOUBLED_MG = -8;
constexpr int DOUBLED_EG = -16;

// Penalty for a pawn that (a) isn't passed, (b) has no same-color pawn
// on an adjacent file positioned to ever support it (i.e. at the same
// or a less advanced rank), and (c) can't safely advance because an
// enemy pawn controls the square directly ahead.
constexpr int BACKWARD_MG = -8;
constexpr int BACKWARD_EG = -12;

// Detect whether a pawn is passed: no enemy pawns on the pawn's file or
// adjacent files ahead of it (in the direction of advancement).
bool is_passed_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s), r = rank_of(s);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    for (int nf = std::max(0, f - 1); nf <= std::min(7, f + 1); ++nf) {
        for (int nr = 0; nr < 8; ++nr) {
            bool ahead = (c == WHITE) ? (nr > r) : (nr < r);
            if (!ahead) continue;
            if (enemy_pawns & square_bb(make_square(nf, nr))) return false;
        }
    }
    return true;
}

bool is_isolated_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard neighbor_files = 0;
    if (f > 0) neighbor_files |= FILE_BB[f - 1];
    if (f < 7) neighbor_files |= FILE_BB[f + 1];
    return (own_pawns & neighbor_files) == 0;
}

// A pawn of color `by` attacks square `s`? Reverse trick matching
// Position::square_attacked_by()'s pattern (src/position.cpp), but
// restricted to pawns only - the backward-pawn definition below
// specifically means "an enemy pawn controls the stop square", not
// "any enemy piece".
bool pawn_attacks_square(const Position& pos, Square s, Color by) {
    Color opponent = Color(by ^ 1);
    return (pawn_attacks(opponent, s) & pos.pieces(by, PAWN)) != 0;
}

// Precondition: `s` is not a passed pawn (the caller checks this first,
// so this doesn't redundantly recompute is_passed_pawn()).
bool is_backward_pawn(const Position& pos, Color c, Square s) {
    int f = file_of(s);
    int rel_r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
    Bitboard own_pawns = pos.pieces(c, PAWN);

    for (int nf = f - 1; nf <= f + 1; nf += 2) {
        if (nf < 0 || nf > 7) continue;
        Bitboard neighbors = own_pawns & FILE_BB[nf];
        while (neighbors) {
            Square ns = pop_lsb(neighbors);
            int neighbor_rel_r = (c == WHITE) ? rank_of(ns) : 7 - rank_of(ns);
            if (neighbor_rel_r <= rel_r) return false;
        }
    }

    Color them = Color(c ^ 1);
    Square stop_sq = make_square(f, rank_of(s) + (c == WHITE ? 1 : -1));
    return pawn_attacks_square(pos, stop_sq, them);
}

void pawn_structure_for_color(const Position& pos, Color c, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    Bitboard pawns = pos.pieces(c, PAWN);
    Bitboard scan = pawns;
    while (scan) {
        Square s = pop_lsb(scan);
        bool passed = is_passed_pawn(pos, c, s);
        if (passed) {
            int rel_rank = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            mg += PASSED_PAWN_MG[rel_rank];
            eg += PASSED_PAWN_EG[rel_rank];
        } else if (is_backward_pawn(pos, c, s)) {
            mg += BACKWARD_MG;
            eg += BACKWARD_EG;
        }
        if (is_isolated_pawn(pos, c, s)) {
            mg += ISOLATED_MG;
            eg += ISOLATED_EG;
        }
    }
    for (int f = 0; f < 8; ++f) {
        int count = popcount(pawns & FILE_BB[f]);
        if (count > 1) {
            mg += DOUBLED_MG * (count - 1);
            eg += DOUBLED_EG * (count - 1);
        }
    }
}

struct PawnEntry {
    zobrist::Key key = 0;
    int mg = 0;
    int eg = 0;
};

// Fixed-size, always-replace, key-verified-on-probe (mirrors
// TranspositionTable's design in src/tt.hpp/.cpp) - but with no
// depth/bound fields and no clear(): a pawn structure's score never
// depends on anything outside the pawn placement it was computed from,
// so a stored entry stays valid forever, across positions and games.
constexpr std::size_t PAWN_HASH_SIZE = 1 << 16; // 65536 entries, ~1 MB
constexpr std::size_t PAWN_HASH_MASK = PAWN_HASH_SIZE - 1;
std::vector<PawnEntry> pawn_hash_table(PAWN_HASH_SIZE);

} // namespace

void pawn_structure_uncached(const Position& pos, int& mg, int& eg) {
    int mg_w, eg_w, mg_b, eg_b;
    pawn_structure_for_color(pos, WHITE, mg_w, eg_w);
    pawn_structure_for_color(pos, BLACK, mg_b, eg_b);
    mg = mg_w - mg_b;
    eg = eg_w - eg_b;
}

void pawn_structure(const Position& pos, int& mg, int& eg) {
    zobrist::Key key = pos.pawn_key();
    PawnEntry& entry = pawn_hash_table[key & PAWN_HASH_MASK];
    if (entry.key == key) {
        mg = entry.mg;
        eg = entry.eg;
        return;
    }

    pawn_structure_uncached(pos, mg, eg);
    entry = {key, mg, eg};
}

} // namespace chess
```

- [ ] **Step 5: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Expected: PASS — the new consistency test and every pre-existing test (including all of Tasks 2-4's isolated/doubled/backward tests, since `pawn_structure()`'s observable output is unchanged by adding the cache in front of it).

- [ ] **Step 6: Commit**

```bash
git add src/pawn_eval.hpp src/pawn_eval.cpp tests/test_eval.cpp
git commit -m "$(cat <<'EOF'
perf: cache pawn structure scoring behind a pawn hash table

Keyed on Position::pawn_key(), always-replace, key-verified-on-probe
like the main transposition table - but never cleared, since a given
pawn structure's score never depends on anything else on the board.
EOF
)"
```

---

### Task 6: Validate with SPRT

**Files:** none (measurement only; may produce a follow-up commit if constants need retuning, or a revert if the feature doesn't hold up).

**Interfaces:**
- Consumes: `tools/sprt/run_sprt.sh` (existing script), Task 5's commit as the SPRT candidate, the commit immediately before Task 1's as the baseline.
- Produces: nothing further depends on this — final gate for the whole feature.

- [ ] **Step 1: Confirm SPRT tooling is set up**

```bash
ls tools/sprt/fastchess tools/sprt/books
```

Expected: both exist and are non-empty. If not, run the one-time setup first:

```bash
tools/sprt/setup.sh
```

- [ ] **Step 2: Identify the baseline commit**

```bash
git log --oneline -8
```

Find the commit hash of the spec-doc commit ("docs: add design spec for pawn structure eval...") — that is the baseline (master immediately before Task 1's work). Call it `<baseline>` below.

- [ ] **Step 3: Run the SPRT match**

```bash
tools/sprt/run_sprt.sh <baseline> HEAD
```

This runs until fastchess's LLR crosses a bound (or `SPRT_ROUNDS` is exhausted) and prints one of:
- `H1 accepted` — the candidate (isolated + doubled + backward + pawn hash) is a confirmed strength gain.
- `H0 accepted` — no gain, or a regression.

This can take anywhere from several minutes to a couple of hours depending on hardware; safe to run in the background and check back.

- [ ] **Step 4a: If "H1 accepted" — done**

No further action. Tasks 1-5's commits stand as-is; the feature is validated and kept.

- [ ] **Step 4b: If "H0 accepted" (wash or regression) — retune once**

Edit the six constants in `src/pawn_eval.cpp` to more conservative literature values:

```cpp
constexpr int ISOLATED_MG = -8;
constexpr int ISOLATED_EG = -12;
constexpr int DOUBLED_MG = -5;
constexpr int DOUBLED_EG = -10;
constexpr int BACKWARD_MG = -5;
constexpr int BACKWARD_EG = -8;
```

Rebuild and rerun the full test suite (Tasks 2-4's directional `CHECK(a < b)` tests don't depend on exact magnitudes, only on the penalty being negative and non-zero, so they should still pass unchanged; confirm this rather than assume it):

```bash
cmake --build build --config Release
./build/Release/chess_tests.exe
```

Commit as a follow-up:

```bash
git add src/pawn_eval.cpp
git commit -m "$(cat <<'EOF'
tune: reduce pawn structure penalty magnitudes

First attempt (isolated -12/-18, doubled -8/-16, backward -8/-12)
didn't clear SPRT; narrowing towards the conservative end of the
literature range trades signal strength for safety.
EOF
)"
```

Then repeat Step 3 (`tools/sprt/run_sprt.sh <baseline> HEAD`, still comparing against the same pre-feature baseline). If this second attempt also reports "H0 accepted", the feature isn't a net gain for this engine — revert all commits back to `<baseline>` and stop:

```bash
git revert --no-edit HEAD HEAD~1 HEAD~2 HEAD~3 HEAD~4
```

(Five commits to revert: the Task 6 retune, and Tasks 5/4/3/2/1's feature commits — adjust the count if Step 4b's retune commit doesn't exist because H0 was hit on the very first SPRT run.)

---

## Self-Review

**Spec coverage:** Architecture (pawn_key + pawn_eval.hpp/cpp + hash) → Tasks 1, 2, 5. Isolated/doubled/backward definitions and values → Tasks 2, 3, 4 (exact constants match the spec's table). Passed-pawn consolidation → Task 2. Testing strategy (per-feature FEN tests, pawn hash consistency test) → Tasks 2-5. SPRT validation → Task 6, including the spec's fallback retune-once-then-revert plan. No NPS/`bench` gate → correctly omitted (spec states this explicitly).

**Placeholder scan:** No TBD/TODO; every step has complete, concrete code; every FEN and constant is a real value, not a description.

**Type consistency:** `void pawn_structure(const Position&, int&, int&)` and `void pawn_structure_uncached(const Position&, int&, int&)` are declared identically in Task 5's `pawn_eval.hpp` and used identically in `eval.cpp` (Task 2, unchanged after) and `tests/test_eval.cpp` (Task 5). `Position::pawn_key() const -> zobrist::Key` (Task 1) is used identically in Task 5's `pawn_structure()`. Constant names (`ISOLATED_MG/EG`, `DOUBLED_MG/EG`, `BACKWARD_MG/EG`, `PASSED_PAWN_MG/EG`) are consistent across Tasks 2-5's full-file replacements.
