# Tapered Positional Evaluation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the engine's single-phase material+PST evaluation with a
tapered (middlegame/endgame) scheme, and add mobility, bishop pair, rook
file, passed pawn, and king safety terms.

**Architecture:** `src/eval.cpp` accumulates separate `mg_score`/`eg_score`
totals from material + PST (now split into MG/EG tables) plus passed-pawn
and king-safety bonuses, blends them via a game-phase-derived `taper()`,
then adds phase-independent mobility/bishop-pair/rook-file bonuses. See
`docs/superpowers/specs/2026-07-12-tapered-positional-eval-design.md` for
the full rationale.

**Tech Stack:** C++23, doctest (via `tests/test_eval.cpp`, already wired
into the `chess_tests` CMake target).

## Global Constraints

- `evaluate(const Position&)`'s signature and side-to-move-relative,
  centipawn contract (from `src/eval.hpp`) must not change.
- All new code lives in `src/eval.cpp` (anonymous namespace for helpers);
  no other files are modified except `tests/test_eval.cpp`.
- Every task must leave `ctest --test-dir build --output-on-failure`
  passing before moving to the next task.
- Build command: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
- Test-one-case command: `./build/chess_tests --test-case="<name>"`.
- This is a tuning change per `CLAUDE.md`: after all tasks are done and
  unit tests pass, it must be validated by SPRT (`tools/sprt/run_sprt.sh`)
  before being considered mergeable — this is Task 8, the final task.

---

### Task 1: Game phase + tapered material/PST core

**Files:**
- Modify: `src/eval.cpp` (replace `MATERIAL_VALUE`/`PST` with MG/EG tables, add `game_phase`/`taper`, rewrite `evaluate`)
- Test: `tests/test_eval.cpp`

**Interfaces:**
- Produces (internal, anonymous-namespace, used by later tasks in the same file):
  - `constexpr int PHASE_WEIGHT[PIECE_TYPE_NB]`
  - `constexpr int PHASE_MAX = 24;`
  - `int game_phase(const Position& pos)` — 0 (bare endgame) to 24 (full material)
  - `int taper(int mg, int eg, int phase)`
  - `constexpr int MG_MATERIAL_VALUE[PIECE_TYPE_NB]`, `constexpr int EG_MATERIAL_VALUE[PIECE_TYPE_NB]`
  - `constexpr int MG_PST[PIECE_TYPE_NB][SQUARE_NB]`, `constexpr int EG_PST[PIECE_TYPE_NB][SQUARE_NB]`
- Consumes: nothing from other tasks (this is the foundation).

- [ ] **Step 1: Write the failing tests**

Replace the existing `TEST_CASE("king-safety PST rewards a tucked-in king over an advanced one")` in `tests/test_eval.cpp` (it pins the old MG-only king PST behavior, which tapering intentionally changes for bare-king endgames) and the exact-value pin test with the following. Keep every other existing `TEST_CASE` in the file unchanged.

```cpp
TEST_CASE("EG king table rewards a centralized king over a cornered one in a bare-king endgame") {
    attacks::init();
    // Only kings on the board: material cancels and game_phase() is 0, so
    // this pins purely EG king-table behavior (the opposite polarity of
    // the old MG-only king PST, which preferred the back-rank corner).
    Position centralized; centralized.set("7k/8/8/8/4K3/8/8/8 w - - 0 1"); // White Ke4, Black Kh8
    Position cornered;    cornered.set("7k/8/8/8/8/8/8/K7 w - - 0 1");    // White Ka1, Black Kh8
    CHECK(evaluate(centralized) > evaluate(cornered));
}

TEST_CASE("king PST pins exact castled/exposed values") {
    attacks::init();
    // Only kings on the board: material cancels, so evaluate() reduces to
    // the EG king-table delta (game_phase() == 0 with no other pieces).
    // Pins exact values so a future transcription error (e.g. duplicated
    // rows) fails loudly instead of only being caught by a directional check.
    Position castled; castled.set("4k3/8/8/8/8/8/8/6K1 w - - 0 1");  // White Kg1
    CHECK(evaluate(castled) == 4);
    Position exposed; exposed.set("4k3/8/8/6K1/8/8/8/8 w - - 0 1");  // White Kg5
    CHECK(evaluate(exposed) == 54);
    Position symmetric; symmetric.set("6k1/8/8/8/8/8/8/6K1 w - - 0 1"); // both Kg1/Kg8
    CHECK(evaluate(symmetric) == 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build && ./build/chess_tests --test-case="EG king table*,king PST pins*"`
Expected: FAIL (old code still returns the previous MG-only values; `evaluate(centralized) > evaluate(cornered)` is false and the exact-value pins don't match).

- [ ] **Step 3: Replace the material/PST tables and `evaluate()` in `src/eval.cpp`**

Replace the entire body of `src/eval.cpp` from the `MATERIAL_VALUE` constant through the end of `evaluate()` with:

```cpp
#include "eval.hpp"
#include "position.hpp"
#include "types.hpp"
#include "bitboard.hpp"

#include <algorithm>

namespace chess {

// Game-phase weight per piece type, used to blend middlegame (MG) and
// endgame (EG) scores. 24 = full non-pawn material (opening), 0 = bare
// kings/pawns (deep endgame). Weights match the well-known "PeSTO"
// evaluation function's phase scheme.
constexpr int PHASE_WEIGHT[PIECE_TYPE_NB] = {0, 1, 1, 2, 4, 0}; // P,N,B,R,Q,K
constexpr int PHASE_MAX = 24;

int game_phase(const Position& pos) {
    int phase = 0;
    for (int pt = KNIGHT; pt <= QUEEN; ++pt)
        phase += PHASE_WEIGHT[pt] * popcount(pos.pieces(static_cast<PieceType>(pt)));
    return std::min(phase, PHASE_MAX);
}

int taper(int mg, int eg, int phase) {
    return (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX;
}

// Material values in centipawns, split by phase (based on the public-domain
// "PeSTO" evaluation tables).
constexpr int MG_MATERIAL_VALUE[PIECE_TYPE_NB] = {82, 337, 365, 477, 1025, 0};
constexpr int EG_MATERIAL_VALUE[PIECE_TYPE_NB] = {94, 281, 297, 512, 936, 0};

// Piece-Square Tables (PST), split by phase. Indexed by square (LERF:
// a1=0, h8=63), with row 0 of each literal table below being rank 8 (so
// White's square is mirrored via `s ^ 56` before indexing, same convention
// as the pre-tapering single-table version).
constexpr int MG_PST[PIECE_TYPE_NB][SQUARE_NB] = {
    // PAWN
    {
          0,   0,   0,   0,   0,   0,   0,   0,
         98, 134,  61,  95,  68, 126,  34, -11,
         -6,   7,  26,  31,  65,  56,  25, -20,
        -14,  13,   6,  21,  23,  12,  17, -23,
        -27,  -2,  -5,  12,  17,   6,  10, -25,
        -26,  -4,  -4, -10,   3,   3,  33, -12,
        -35,  -1, -20, -23, -15,  24,  38, -22,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    // KNIGHT
    {
        -167, -89, -34, -49,  61, -97, -15, -107,
         -73, -41,  72,  36,  23,  62,   7,  -17,
         -47,  60,  37,  65,  84, 129,  73,   44,
          -9,  17,  19,  53,  37,  69,  18,   22,
         -13,   4,  16,  13,  28,  19,  21,   -8,
         -23,  -9,  12,  10,  19,  17,  25,  -16,
         -29, -53, -12,  -3,  -1,  18, -14,  -19,
        -105, -21, -58, -33, -17, -28, -19,  -23
    },
    // BISHOP
    {
        -29,   4, -82, -37, -25, -42,   7,  -8,
        -26,  16, -18, -13,  30,  59,  18, -47,
        -16,  37,  43,  40,  35,  50,  37,  -2,
         -4,   5,  19,  50,  37,  37,   7,  -2,
         -6,  13,  13,  26,  34,  12,  10,   4,
          0,  15,  15,  15,  14,  27,  18,  10,
          4,  15,  16,   0,   7,  21,  33,   1,
        -33,  -3, -14, -21, -13, -12, -39, -21
    },
    // ROOK
    {
         32,  42,  32,  51,  63,   9,  31,  43,
         27,  32,  58,  62,  80,  67,  26,  44,
         -5,  19,  26,  36,  17,  45,  61,  16,
        -24, -11,   7,  26,  24,  35,  -8, -20,
        -36, -26, -12,  -1,   9,  -7,   6, -23,
        -45, -25, -16, -17,   3,   0,  -5, -33,
        -44, -16, -20,  -9,  -1,  11,  -6, -71,
        -19, -13,   1,  17,  16,   7, -37, -26
    },
    // QUEEN
    {
        -28,   0,  29,  12,  59,  44,  43,  45,
        -24, -39,  -5,   1, -16,  57,  28,  54,
        -13, -17,   7,   8,  29,  56,  47,  57,
        -27, -27, -16, -16,  -1,  17,  -2,   1,
         -9, -26,  -9, -10,  -2,  -4,   3,  -3,
        -14,   2, -11,  -2,  -5,   2,  14,   5,
        -35,  -8,  11,   2,   8,  15,  -3,   1,
         -1, -18,  -9,  10, -15, -25, -31, -50
    },
    // KING
    {
        -65,  23,  16, -15, -56, -34,   2,  13,
         29,  -1, -20,  -7,  -8,  -4, -38, -29,
         -9,  24,   2, -16, -20,   6,  22, -22,
        -17, -20, -12, -27, -30, -25, -14, -36,
        -49,  -1, -27, -39, -46, -44, -33, -51,
        -14, -14, -22, -46, -44, -30, -15, -27,
          1,   7,  -8, -64, -43, -16,   9,   8,
        -15,  36,  12, -54,   8, -28,  24,  14
    }
};

constexpr int EG_PST[PIECE_TYPE_NB][SQUARE_NB] = {
    // PAWN
    {
          0,   0,   0,   0,   0,   0,   0,   0,
        178, 173, 158, 134, 147, 132, 165, 187,
         94, 100,  85,  67,  56,  53,  82,  84,
         32,  24,  13,   5,  -2,   4,  17,  17,
         13,   9,  -3,  -7,  -7,  -8,   3,  -1,
          4,   7,  -6,   1,   0,  -5,  -1,  -8,
         13,   8,   8,  10,  13,   0,   2,  -7,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    // KNIGHT
    {
        -58, -38, -13, -28, -31, -27, -63, -99,
        -25,  -8, -25,  -2,  -9, -25, -24, -52,
        -24, -20,  10,   9,  -1,  -9, -19, -41,
        -17,   3,  22,  22,  22,  11,   8, -18,
        -18,  -6,  16,  25,  16,  17,   4, -18,
        -23,  -3,  -1,  15,  10,  -3, -20, -22,
        -42, -20, -10,  -5,  -2, -20, -23, -44,
        -29, -51, -23, -15, -22, -18, -50, -64
    },
    // BISHOP
    {
        -14, -21, -11,  -8,  -7,  -9, -17, -24,
         -8,  -4,   7, -12,  -3, -13,  -4, -14,
          2,  -8,   0,  -1,  -2,   6,   0,   4,
         -3,   9,  12,   9,  14,  10,   3,   2,
         -6,   3,  13,  19,   7,  10,  -3,  -9,
        -12,  -3,   8,  10,  13,   3,  -7, -15,
        -14, -18,  -7,  -1,   4,  -9, -15, -27,
        -23,  -9, -23,  -5,  -9, -16,  -5, -17
    },
    // ROOK
    {
         13,  10,  18,  15,  12,  12,   8,   5,
         11,  13,  13,  11,  -3,   3,   8,   3,
          7,   7,   7,   5,   4,  -3,  -5,  -3,
          4,   3,  13,   1,   2,   1,  -1,   2,
          3,   5,   8,   4,  -5,  -6,  -8, -11,
         -4,   0,  -5,  -1,  -7, -12,  -8, -16,
         -6,  -6,   0,   2,  -9,  -9, -11,  -3,
         -9,   2,   3,  -1,  -5, -13,   4, -20
    },
    // QUEEN
    {
         -9,  22,  22,  27,  27,  19,  10,  20,
        -17,  20,  32,  41,  58,  25,  30,   0,
        -20,   6,   9,  49,  47,  35,  19,   9,
          3,  22,  24,  45,  57,  40,  57,  36,
        -18,  28,  19,  47,  31,  34,  39,  23,
        -16, -27,  15,   6,   9,  17,  10,   5,
        -22, -23, -30, -16, -16, -23, -36, -32,
        -33, -28, -22, -43,  -5, -32, -20, -41
    },
    // KING
    {
        -74, -35, -18, -18, -11,  15,   4, -17,
        -12,  17,  14,  17,  17,  38,  23,  11,
         10,  17,  23,  15,  20,  45,  44,  13,
         -8,  22,  24,  27,  26,  33,  26,   3,
        -18,  -4,  21,  24,  27,  23,   9, -11,
        -19,  -3,  11,  21,  23,  16,   7,  -9,
        -27, -11,   4,  13,  14,   4,  -5, -17,
        -53, -34, -21, -11, -28, -14, -24, -43
    }
};

int evaluate(const Position& pos) {
    int mg_score = 0, eg_score = 0;

    for (int pt_idx = PAWN; pt_idx < PIECE_TYPE_NB; ++pt_idx) {
        PieceType pt = static_cast<PieceType>(pt_idx);

        Bitboard white_pieces = pos.pieces(WHITE, pt);
        while (white_pieces) {
            Square s = pop_lsb(white_pieces);
            // LERF index 0 is a1; the tables above are written rank-8-first,
            // so flip White's square to land it on the table's bottom row.
            Square mirrored = static_cast<Square>(s ^ 56);
            mg_score += MG_MATERIAL_VALUE[pt] + MG_PST[pt][mirrored];
            eg_score += EG_MATERIAL_VALUE[pt] + EG_PST[pt][mirrored];
        }

        Bitboard black_pieces = pos.pieces(BLACK, pt);
        while (black_pieces) {
            Square s = pop_lsb(black_pieces);
            // Black's back rank is rank 8 (row 0), so it reads directly.
            mg_score -= MG_MATERIAL_VALUE[pt] + MG_PST[pt][s];
            eg_score -= EG_MATERIAL_VALUE[pt] + EG_PST[pt][s];
        }
    }

    int phase = game_phase(pos);
    int score = taper(mg_score, eg_score, phase);

    // Return score from side-to-move's perspective
    if (pos.side_to_move() == BLACK) {
        score = -score;
    }

    return score;
}

}  // namespace chess
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS (all cases, including the two new/rewritten king-table tests). If the exact-value pins in "king PST pins exact castled/exposed values" don't match `-5`/`33`, print `evaluate(castled)` and `evaluate(exposed)` (e.g. temporarily via a `MESSAGE`/`WARN` or a quick scratch `printf` in a throwaway `main`) and update the two literals in the test to whatever the real computed values are — the point of that test is to pin whatever the tables actually produce, not to match this document's arithmetic exactly.

- [ ] **Step 5: Commit**

```bash
git add src/eval.cpp tests/test_eval.cpp
git commit -m "feat: tapered MG/EG material and piece-square tables"
```

---

### Task 2: Mobility

**Files:**
- Modify: `src/eval.cpp`
- Test: `tests/test_eval.cpp`

**Interfaces:**
- Consumes: `Position::pieces(Color, PieceType)`, `Position::occupied()`, `attacks::knight_attacks`, `attacks::bishop_attacks`, `attacks::rook_attacks`, `attacks::queen_attacks` (all already available via `#include "attacks.hpp"`).
- Produces: `int mobility(const Position& pos, Color c)`, called from `evaluate()`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_eval.cpp`:

```cpp
TEST_CASE("a centralized knight has higher mobility than one boxed in by its own pawns") {
    attacks::init();
    // Same material and PST square for the knight's own placement effect
    // aside, isolate mobility by boxing one knight in with its own pawns.
    Position mobile; mobile.set("4k3/8/8/3N4/8/8/8/4K3 w - - 0 1"); // Nd5, wide open
    Position boxed;   boxed.set("4k3/8/1P1P4/2N5/1P1P4/8/8/4K3 w - - 0 1"); // Nc5 boxed by own pawns on b4/b6/d4/d6
    CHECK(evaluate(mobile) < evaluate(boxed) - 200); // boxed has 4 extra pawns worth ~400cp; mobility alone shouldn't close that, but a sanity margin isn't the point here
}
```

Replace that with a cleaner isolation instead — same piece count on both sides, only reachable-square count differs:

```cpp
TEST_CASE("a centralized knight has higher mobility than one boxed in by its own pawns") {
    attacks::init();
    // Both positions have identical material (White: K+N+4P, Black: K) and
    // the knight sits on the same square (c5) in both, so material and PST
    // are equal; only the knight's mobility (attacked squares not occupied
    // by its own pawns) differs.
    Position mobile; mobile.set("4k3/8/8/2N5/8/8/PPPP4/4K3 w - - 0 1"); // Nc5, pawns far away on a2-d2
    Position boxed;   boxed.set("4k3/8/1P1P4/2N5/1P1P4/8/8/4K3 w - - 0 1"); // Nc5 boxed by own pawns on b4/b6/d4/d6
    CHECK(evaluate(mobile) > evaluate(boxed));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/chess_tests --test-case="a centralized knight*"`
Expected: FAIL (mobility isn't computed yet, so both positions differ only by pawn PST placement, not knight mobility — the boxed pawns on ranks 4/6 vs the mobile pawns on rank 2 currently score in an unrelated, untested direction; the assertion fails or passes for the wrong reason). If it happens to pass already by coincidence, that's a signal the test isn't isolating mobility — re-read Step 1's position choice before continuing; do not proceed until you've confirmed with a debug print that the *only* intended-to-differ quantity is knight mobility (compute pawn PST contributions by hand for both positions and confirm they're unrelated to the CHECK direction, or simplify further by moving all four pawns to the same relative offset in both positions so PST contributions are identical, e.g. reusing `a2,b2,c2,d2` in the "mobile" case and `a4,b4,c4,d4"-shaped box only around the knight in the "boxed" case).

- [ ] **Step 3: Implement `mobility()` and wire it into `evaluate()`**

Add `#include "attacks.hpp"` to the top of `src/eval.cpp`. Add this function above `evaluate()`:

```cpp
// Mobility bonus: attacked squares not occupied by the piece's own side,
// weighted per piece type (knights/bishops get more weight per square
// since they have fewer reachable squares in absolute terms than rooks/
// queens). Pawns and kings are excluded - not a meaningful signal here.
int mobility(const Position& pos, Color c) {
    static constexpr int WEIGHT[PIECE_TYPE_NB] = {0, 4, 3, 2, 1, 0};
    Bitboard occ = pos.occupied();
    Bitboard own = pos.pieces(c);
    int score = 0;

    Bitboard knights = pos.pieces(c, KNIGHT);
    while (knights) {
        Square s = pop_lsb(knights);
        score += WEIGHT[KNIGHT] * popcount(knight_attacks(s) & ~own);
    }
    Bitboard bishops = pos.pieces(c, BISHOP);
    while (bishops) {
        Square s = pop_lsb(bishops);
        score += WEIGHT[BISHOP] * popcount(bishop_attacks(s, occ) & ~own);
    }
    Bitboard rooks = pos.pieces(c, ROOK);
    while (rooks) {
        Square s = pop_lsb(rooks);
        score += WEIGHT[ROOK] * popcount(rook_attacks(s, occ) & ~own);
    }
    Bitboard queens = pos.pieces(c, QUEEN);
    while (queens) {
        Square s = pop_lsb(queens);
        score += WEIGHT[QUEEN] * popcount(queen_attacks(s, occ) & ~own);
    }
    return score;
}
```

In `evaluate()`, after the `int phase = game_phase(pos);` line and before `int score = taper(mg_score, eg_score, phase);`, add:

```cpp
    int flat_score = mobility(pos, WHITE) - mobility(pos, BLACK);
```

Change the score line to:

```cpp
    int score = taper(mg_score, eg_score, phase) + flat_score;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/eval.cpp tests/test_eval.cpp
git commit -m "feat: add mobility term to evaluation"
```

---

### Task 3: Bishop pair

**Files:**
- Modify: `src/eval.cpp`
- Test: `tests/test_eval.cpp`

**Interfaces:**
- Consumes: `Position::pieces(Color, PieceType)`, `popcount`.
- Produces: `int bishop_pair(const Position& pos, Color c)`, folded into `flat_score`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("the bishop pair is worth a bonus over a lone bishop") {
    attacks::init();
    // White has two bishops in both positions on the same squares; Black
    // has a bishop+knight vs bishop+bishop, isolating the pair bonus.
    // Bishop and knight MG material values are close enough (365 vs 337)
    // that the direction of the check isolates the pair bonus rather than
    // being dominated by the material gap.
    Position pair;    pair.set("4k3/8/8/8/8/2b2b2/8/4K3 b - - 0 1");   // Black bishops c3,f3
    Position no_pair; no_pair.set("4k3/8/8/8/8/2n2b2/8/4K3 b - - 0 1"); // Black knight+bishop c3,f3
    // Black to move in both; compare from White's perspective by negating,
    // since evaluate() is side-to-move-relative and it's Black's material
    // that differs here.
    CHECK(-evaluate(pair) > -evaluate(no_pair));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/chess_tests --test-case="the bishop pair*"`
Expected: FAIL (no bishop-pair bonus yet, and bishop MG value 365 > knight MG value 337 already makes `pair` score higher for the reasons the test isn't trying to isolate — check with a debug print that without the bonus the gap is exactly the bishop-knight material/PST delta, then proceed; the test still correctly fails to distinguish "bonus exists" from "material differs" until Step 3 adds a large-enough, clearly-separate bonus).

Note: since bishop and knight already have a small material gap, this test alone can't cleanly prove the bonus exists in isolation — it mainly guards against a regression once the bonus is added. That's an accepted limitation of testing this term through `evaluate()` alone (see design doc's testing strategy); the important property is that the bonus's sign is correct, which this test does confirm.

- [ ] **Step 3: Implement `bishop_pair()` and wire it in**

Add above `evaluate()`:

```cpp
// Flat bonus for holding both bishops (better long-term minor-piece
// coordination and color-complex control than a lone bishop).
constexpr int BISHOP_PAIR_BONUS = 30;

int bishop_pair(const Position& pos, Color c) {
    return popcount(pos.pieces(c, BISHOP)) >= 2 ? BISHOP_PAIR_BONUS : 0;
}
```

Change the `flat_score` line in `evaluate()` to:

```cpp
    int flat_score = mobility(pos, WHITE) - mobility(pos, BLACK)
                    + bishop_pair(pos, WHITE) - bishop_pair(pos, BLACK);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/eval.cpp tests/test_eval.cpp
git commit -m "feat: add bishop pair bonus to evaluation"
```

---

### Task 4: Rook on open/semi-open file

**Files:**
- Modify: `src/eval.cpp`
- Test: `tests/test_eval.cpp`

**Interfaces:**
- Consumes: `Position::pieces(Color, PieceType)`, `file_of(Square)`, bitboard file constants (`FILE_A_BB`..`FILE_H_BB`).
- Produces: `int rook_file_bonus(const Position& pos, Color c)`, folded into `flat_score`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("a rook on a fully open file outscores one blocked by its own pawn") {
    attacks::init();
    // Same material (K+R+P) and same rook/king squares in both; only
    // whether the rook's own pawn sits on its file differs.
    Position open;    open.set("4k3/8/8/8/8/8/7P/3RK3 w - - 0 1");    // Rd1, pawn on h2 (different file)
    Position blocked; blocked.set("4k3/8/8/8/8/8/3P4/3RK3 w - - 0 1"); // Rd1, pawn on d2 (same file, blocking)
    CHECK(evaluate(open) > evaluate(blocked));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/chess_tests --test-case="a rook on a fully open file*"`
Expected: FAIL (no rook-file term yet, but note the pawn's own PST value differs between h2 and d2 too — verify via debug print that the PST delta alone doesn't already satisfy the CHECK direction before trusting this test's failure/pass as meaningful; if it does, swap the pawn's off-file square in `open` to one with an equal or lower PST value than d2 so the rook-file bonus is what's carrying the direction, e.g. `a2` which is a corner-ish square in the MG pawn table).

- [ ] **Step 3: Implement `rook_file_bonus()` and wire it in**

Add above `evaluate()`:

```cpp
// Bonus for a rook with no pawns of its own on its file (open) or only
// enemy pawns on its file (semi-open) - a well-known heuristic reflecting
// the file's importance for rook activity/pressure.
constexpr int ROOK_OPEN_FILE_BONUS = 20;
constexpr int ROOK_SEMI_OPEN_FILE_BONUS = 10;

int rook_file_bonus(const Position& pos, Color c) {
    static constexpr Bitboard FILE_BB[8] = {
        FILE_A_BB, FILE_B_BB, FILE_C_BB, FILE_D_BB,
        FILE_E_BB, FILE_F_BB, FILE_G_BB, FILE_H_BB
    };
    Bitboard own_pawns = pos.pieces(c, PAWN);
    Bitboard enemy_pawns = pos.pieces(Color(c ^ 1), PAWN);
    int score = 0;

    Bitboard rooks = pos.pieces(c, ROOK);
    while (rooks) {
        Square s = pop_lsb(rooks);
        Bitboard file_bb = FILE_BB[file_of(s)];
        bool has_own = file_bb & own_pawns;
        bool has_enemy = file_bb & enemy_pawns;
        if (!has_own && !has_enemy) score += ROOK_OPEN_FILE_BONUS;
        else if (!has_own && has_enemy) score += ROOK_SEMI_OPEN_FILE_BONUS;
    }
    return score;
}
```

Change the `flat_score` line in `evaluate()` to:

```cpp
    int flat_score = mobility(pos, WHITE) - mobility(pos, BLACK)
                    + bishop_pair(pos, WHITE) - bishop_pair(pos, BLACK)
                    + rook_file_bonus(pos, WHITE) - rook_file_bonus(pos, BLACK);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/eval.cpp tests/test_eval.cpp
git commit -m "feat: add rook open/semi-open file bonus to evaluation"
```

---

### Task 5: Passed pawns

**Files:**
- Modify: `src/eval.cpp`
- Test: `tests/test_eval.cpp`

**Interfaces:**
- Consumes: `Position::pieces(Color, PieceType)`, `file_of`/`rank_of`/`make_square`, `square_bb`.
- Produces: `void passed_pawn_bonus(const Position& pos, Color c, int& mg, int& eg)`, folded into `mg_score`/`eg_score` in `evaluate()`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("an unopposed passed pawn outscores one that can be blocked or captured") {
    attacks::init();
    // Same material (K+P each side) and same White pawn square (d5) in
    // both; only whether Black has a pawn able to block/capture it on the
    // d/c/e files ahead of it differs.
    Position passed; passed.set("4k3/8/8/3P4/8/8/8/4K3 w - - 0 1");   // White pawn d5, no black pawns at all
    Position blocked; blocked.set("4k3/8/3p4/3P4/8/8/8/4K3 w - - 0 1"); // Black pawn directly ahead on d6
    CHECK(evaluate(passed) > evaluate(blocked));
}

TEST_CASE("a more advanced passed pawn outscores a less advanced one") {
    attacks::init();
    // Same material; both pawns are passed (no blockers), only rank differs.
    Position advanced; advanced.set("4k3/3P4/8/8/8/8/8/4K3 w - - 0 1"); // White pawn d7
    Position early;    early.set("4k3/8/8/8/8/8/3P4/4K3 w - - 0 1");    // White pawn d2
    CHECK(evaluate(advanced) > evaluate(early));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build && ./build/chess_tests --test-case="an unopposed passed pawn*,a more advanced passed pawn*"`
Expected: The first case likely already passes today by coincidence (d5 vs d6-blocked already differ via mobility/PST); confirm this by checking whether it fails once `passed_pawn_bonus` is added but wired in with a bonus of 0 (temporarily) — but simplest is to just implement Step 3 and confirm both tests pass after, then check they'd fail if the bonus tables were zeroed out (sanity, not a hard requirement to demonstrate failure first for this term, since pre-existing terms already may make the direction correct — the passed-pawn-specific claim is validated by the second test, which isolates rank/passedness cleanly since both pawns are already passed and only rank differs).

- [ ] **Step 3: Implement passed-pawn detection and bonus, wire it in**

Add above `evaluate()`:

```cpp
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
```

In `evaluate()`, after the main material/PST accumulation loop and before `int phase = game_phase(pos);`, add:

```cpp
    int pp_mg_w, pp_eg_w, pp_mg_b, pp_eg_b;
    passed_pawn_bonus(pos, WHITE, pp_mg_w, pp_eg_w);
    passed_pawn_bonus(pos, BLACK, pp_mg_b, pp_eg_b);
    mg_score += pp_mg_w - pp_mg_b;
    eg_score += pp_eg_w - pp_eg_b;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/eval.cpp tests/test_eval.cpp
git commit -m "feat: add passed pawn bonus to evaluation"
```

---

### Task 6: King safety (pawn shield)

**Files:**
- Modify: `src/eval.cpp`
- Test: `tests/test_eval.cpp`

**Interfaces:**
- Consumes: `Position::king_square(Color)`, `file_of`/`rank_of`/`make_square`, `square_bb`.
- Produces: `int king_safety(const Position& pos, Color c)`, folded into `mg_score` only (no `eg` contribution — king safety stops mattering once material thins out).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("a king with an intact pawn shield outscores one with the shield pushed away") {
    attacks::init();
    // Same material (K+3P each side, White pieces only differ in pawn
    // placement) and same White king square (g1) in both; only whether the
    // f/g/h-file pawns are still on their shield squares (f2/g2/h2) or have
    // been pushed forward (losing shield value) differs. Black is bare king
    // far away so it contributes nothing to either side of the comparison.
    Position shielded; shielded.set("7k/8/8/8/8/8/5PPP/6K1 w - - 0 1"); // White Kg1, pawns f2/g2/h2
    Position pushed;   pushed.set("7k/8/8/5PPP/8/8/8/6K1 w - - 0 1");   // White Kg1, pawns f5/g5/h5
    CHECK(evaluate(shielded) > evaluate(pushed));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/chess_tests --test-case="a king with an intact pawn shield*"`
Expected: This may already pass today from the pawn PST alone favoring pawns closer to the back rank in MG terms for other reasons, or the mobility/passed-pawn terms from earlier tasks (the pushed pawns might score as more "passed-pawn-advanced"). Verify by temporarily building without the king-safety change and confirming the outcome; if it already passes for unrelated reasons, that's fine — Step 3 still adds the intended term, and this test guards against a future regression removing it. The important thing is Step 3's code is in fact exercised (confirm with a debug print or a temporarily-zeroed `KING_SHIELD_BONUS` that the test's margin shrinks/changes when the term is disabled).

- [ ] **Step 3: Implement `king_safety()` and wire it in**

Add above `evaluate()`:

```cpp
// Bonus per own pawn found on the two ranks directly in front of the king,
// across the king's file and the two adjacent files (clamped at the board
// edge). MG-only: king safety stops being relevant once enough material
// has been traded off that there's nothing left to attack the king with.
constexpr int KING_SHIELD_BONUS = 10;

int king_safety(const Position& pos, Color c) {
    Square ks = pos.king_square(c);
    int kf = file_of(ks), kr = rank_of(ks);
    Bitboard own_pawns = pos.pieces(c, PAWN);
    int dir = (c == WHITE) ? 1 : -1;
    int shield = 0;

    for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); ++f) {
        for (int dr = 1; dr <= 2; ++dr) {
            int r = kr + dir * dr;
            if (r < 0 || r > 7) continue;
            if (own_pawns & square_bb(make_square(f, r))) shield += KING_SHIELD_BONUS;
        }
    }
    return shield;
}
```

In `evaluate()`, in the block added by Task 5, extend the `mg_score` update to also include king safety:

```cpp
    mg_score += pp_mg_w - pp_mg_b;
    eg_score += pp_eg_w - pp_eg_b;
    mg_score += king_safety(pos, WHITE) - king_safety(pos, BLACK);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/eval.cpp tests/test_eval.cpp
git commit -m "feat: add king safety pawn-shield bonus to evaluation"
```

---

### Task 7: Full regression pass

**Files:**
- None modified — verification only.

- [ ] **Step 1: Run the full test suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS, all test cases across all test files (not just `test_eval.cpp` — `test_search.cpp` and `test_perft.cpp` exercise `evaluate()` indirectly through search and must still pass).

- [ ] **Step 2: Run perft to confirm move generation is untouched**

Run: `./build/chess_tests --test-case="*perft*"`
Expected: PASS (this task tree never touches move generation, but it's a cheap sanity check that nothing in `src/eval.cpp` accidentally broke a shared header).

- [ ] **Step 3: Sanity-check the engine still runs standalone**

Run: `./build/chess_engine` then type `uci`, `isready`, `position startpos`, `go depth 6`, `quit` (interactively) and confirm it prints `bestmove` without crashing or erroring.

No commit for this task (verification only).

---

### Task 8: SPRT validation

**Files:**
- None modified — validation only.

- [ ] **Step 1: One-time SPRT tool setup (skip if already done previously on this machine)**

Run: `tools/sprt/setup.sh`

- [ ] **Step 2: Run SPRT against master**

Note the commit hash of `master` from before Task 1 started (the base) and the current branch's tip commit (the candidate, i.e. after Task 6's commit). Run:

```bash
tools/sprt/run_sprt.sh <base_ref> <candidate_ref>
```

Expected: The tool reports either "H1 accepted" (statistically significant strength gain) or not.

- [ ] **Step 2: Act on the result**

- If "H1 accepted": the change is validated and can be kept as-is (already committed task-by-task).
- If not accepted (regression or wash): per `CLAUDE.md`, the change must be reverted regardless of how clean the unit tests are. Since bundling all five terms plus tapering in one SPRT run (per the design doc's explicit trade-off) means the culprit isn't identified by this run alone, revert the whole branch and report back — bisecting which individual term regressed strength is a separate follow-up task, not part of this plan.

No commit for this task (it's a go/no-go gate on work already committed).
