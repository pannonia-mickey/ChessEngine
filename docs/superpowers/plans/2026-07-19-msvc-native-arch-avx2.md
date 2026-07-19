# MSVC Native-Arch AVX2 Build Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the `CHESS_NATIVE_ARCH` CMake option a real effect on MSVC (a fixed `/arch:AVX2`
compile flag), validate it as a pure speed change per the project's NPS-bench protocol, and
correct the existing documentation's inaccurate claim that both `CHESS_NATIVE_ARCH` and
`CHESS_LTO` are no-ops under MSVC (only the former actually was).

**Architecture:** One new `elseif(CHESS_NATIVE_ARCH AND MSVC)` branch in `CMakeLists.txt`,
mirroring the existing GCC/Clang branch's defensive `check_cxx_compiler_flag` pattern, adds
`/arch:AVX2` to the same `CHESS_TUNING_COMPILE_OPTIONS` list already applied to `chess_lib`,
`chess_engine`, and `chess_tests`. No runtime CPU detection - the flag is a fixed default,
switchable off via the existing `-DCHESS_NATIVE_ARCH=OFF`.

**Tech Stack:** CMake 3.20+, MSVC (Visual Studio generator, multi-config), C++23.

**Spec:** `docs/superpowers/specs/2026-07-19-msvc-native-arch-avx2-design.md`

## Global Constraints

- Windows/MSVC only. This plan's commands assume the Visual Studio multi-config generator
  already configured in this repo's `build/` directory, where the built binary lives at
  `build/Release/chess_engine.exe` (not `build/chess_engine.exe` - that path is only valid for
  single-config generators like Ninja/Makefiles, e.g. on Linux/Raspberry Pi).
- This is a **pure speed change** per `CLAUDE.md`'s protocol (see also
  `docs/perf/nps-baseline.md`): it may be kept only if `chess_engine bench <fix depth>` produces
  **bit-for-bit identical "Nodes searched"** between the flag off and on, **and** a measurably
  higher median NPS across >= 3 runs each. If node counts differ, STOP - this is not a pure
  speed change and must not be kept under this plan.
- Do not touch the `CHESS_LTO` block in `CMakeLists.txt` - it is already active on MSVC
  (confirmed: `WholeProgramOptimization=true` in the generated `.vcxproj` files).
- Follow the existing `CMakeLists.txt` code style: 2-space indentation, comments explaining the
  *why*, `check_cxx_compiler_flag` guards before adding any new compiler flag.

## File Structure

- **Modify `CMakeLists.txt`** (lines 27-43): add the MSVC `/arch:AVX2` branch to the existing
  `CHESS_NATIVE_ARCH` block.
- **Modify `docs/perf/nps-baseline.md`**: correct the MSVC baseline's claim about `CHESS_LTO`
  being a no-op, and add a new measured section for `CHESS_NATIVE_ARCH`'s MSVC effect.
- **Modify `README.md`**: correct the "both are no-ops under MSVC" sentence.
- **Modify `CLAUDE.md`**: correct the equivalent sentence in the "Build, lint, teszt" section.

No new files, no source (`src/`) changes - this plan is entirely build configuration and docs.

---

### Task 1: Add MSVC `/arch:AVX2` branch and verify it toggles correctly

**Files:**
- Modify: `CMakeLists.txt:27-43`

**Interfaces:**
- Consumes: the existing `CHESS_NATIVE_ARCH` option and `CHESS_TUNING_COMPILE_OPTIONS` list
  (both already defined earlier in `CMakeLists.txt`, unchanged by this task).
- Produces: `CHESS_TUNING_COMPILE_OPTIONS` now includes `/arch:AVX2` on MSVC when
  `CHESS_NATIVE_ARCH` is `ON` (the default) and the compiler accepts the flag. Consumed by
  Task 2's bench comparison.

- [ ] **Step 1: Edit `CMakeLists.txt`**

Replace this existing block (lines 27-43):

```cmake
if(CHESS_NATIVE_ARCH AND NOT MSVC)
  include(CheckCXXCompilerFlag)
  # std::popcount/std::countr_zero (see src/bitboard.hpp) only lower to
  # hardware POPCNT/TZCNT/CLZ instructions when the target ISA advertises
  # them; without an arch flag GCC/Clang assume a conservative baseline and
  # emit a software fallback instead. Try the x86 spelling first, then the
  # aarch64 one (e.g. Raspberry Pi), so this stays portable across both.
  check_cxx_compiler_flag("-march=native" CHESS_HAS_MARCH_NATIVE)
  if(CHESS_HAS_MARCH_NATIVE)
    list(APPEND CHESS_TUNING_COMPILE_OPTIONS "-march=native")
  else()
    check_cxx_compiler_flag("-mcpu=native" CHESS_HAS_MCPU_NATIVE)
    if(CHESS_HAS_MCPU_NATIVE)
      list(APPEND CHESS_TUNING_COMPILE_OPTIONS "-mcpu=native")
    endif()
  endif()
endif()
```

with this (adds the `elseif` branch; the `if` branch above is unchanged):

```cmake
if(CHESS_NATIVE_ARCH AND NOT MSVC)
  include(CheckCXXCompilerFlag)
  # std::popcount/std::countr_zero (see src/bitboard.hpp) only lower to
  # hardware POPCNT/TZCNT/CLZ instructions when the target ISA advertises
  # them; without an arch flag GCC/Clang assume a conservative baseline and
  # emit a software fallback instead. Try the x86 spelling first, then the
  # aarch64 one (e.g. Raspberry Pi), so this stays portable across both.
  check_cxx_compiler_flag("-march=native" CHESS_HAS_MARCH_NATIVE)
  if(CHESS_HAS_MARCH_NATIVE)
    list(APPEND CHESS_TUNING_COMPILE_OPTIONS "-march=native")
  else()
    check_cxx_compiler_flag("-mcpu=native" CHESS_HAS_MCPU_NATIVE)
    if(CHESS_HAS_MCPU_NATIVE)
      list(APPEND CHESS_TUNING_COMPILE_OPTIONS "-mcpu=native")
    endif()
  endif()
elseif(CHESS_NATIVE_ARCH AND MSVC)
  include(CheckCXXCompilerFlag)
  # MSVC has no "native" flag - cl.exe never auto-detects the host CPU the
  # way -march=native does. /arch:AVX2 is a fixed stand-in: virtually every
  # x86-64 CPU since ~2013 supports it, and (like the GCC/Clang branch
  # above) it lets std::popcount/std::countr_zero (src/bitboard.hpp) lower
  # to hardware POPCNT/BMI instructions instead of a software fallback.
  # Checked defensively in case an unusual toolset rejects it.
  check_cxx_compiler_flag("/arch:AVX2" CHESS_HAS_ARCH_AVX2)
  if(CHESS_HAS_ARCH_AVX2)
    list(APPEND CHESS_TUNING_COMPILE_OPTIONS "/arch:AVX2")
  endif()
endif()
```

- [ ] **Step 2: Reconfigure with the default (ON) setting**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
Expected: ends with `-- Generating done` and `-- Build files have been written to...`, exit code 0,
no CMake errors.

- [ ] **Step 3: Verify the AVX2 flag is present when `CHESS_NATIVE_ARCH` is ON**

Run: `grep -in avx2 build/chess_lib.vcxproj build/chess_engine.vcxproj`
Expected: at least one matching line in each file (either an `<AdditionalOptions>` entry
containing `/arch:AVX2`, or an `<EnableEnhancedInstructionSet>AdvancedVectorExtensions2</...>`
tag - either form confirms the flag reached the compiler).

- [ ] **Step 4: Verify the flag disappears when `CHESS_NATIVE_ARCH` is OFF**

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHESS_NATIVE_ARCH=OFF
grep -in avx2 build/chess_lib.vcxproj build/chess_engine.vcxproj
```
Expected: the `cmake` call succeeds (exit 0); the `grep` call finds **no matches** (grep exits
with status 1 and prints nothing) - confirming the branch is correctly gated by the option.

- [ ] **Step 5: Restore the default (ON) and do a full build**

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHESS_NATIVE_ARCH=ON
cmake --build build --config Release
```
Expected: build succeeds with no errors (warnings are fine/expected as before this change).

- [ ] **Step 6: Run the full test suite to confirm no regression**

Run: `ctest --test-dir build -C Release --output-on-failure`
Expected: all tests pass (`100% tests passed, 0 tests failed out of N`). A pure compiler-flag
change must not alter behavior observable by the test suite (perft counts, search results, etc.).

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt
git commit -m "perf: target /arch:AVX2 on MSVC via CHESS_NATIVE_ARCH"
```

---

### Task 2: Bench-validate the change (Nodes-searched parity + NPS improvement)

**Files:** none created or modified - this is a measurement task.

**Interfaces:**
- Consumes: the `CHESS_NATIVE_ARCH` toggle from Task 1, and `chess_engine bench <depth>`
  (`src/bench.cpp`), which prints exactly these three lines per run:
  ```
  Total time (ms) : <N>
  Nodes searched  : <N>
  Nodes/second    : <N>
  ```
- Produces: 3 recorded `(time_ms, nodes, nps)` triples for `CHESS_NATIVE_ARCH=OFF` ("bázis") and
  3 for `CHESS_NATIVE_ARCH=ON` ("jelölt"), both at depth 8. Consumed by Task 3's documentation
  update.

This task builds the *same* commit twice, toggling only `CHESS_NATIVE_ARCH`, so the measured
difference is attributable solely to the `/arch:AVX2` flag - no other variable (commit, machine,
background load type) changes between the two measurements.

- [ ] **Step 1: Build the "bázis" (baseline, flag OFF) binary**

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHESS_NATIVE_ARCH=OFF
cmake --build build --config Release --target chess_engine
```
Expected: build succeeds, exit code 0.

- [ ] **Step 2: Run bench 3 times on the bázis binary, record output**

Run three times: `build/Release/chess_engine.exe bench 8`
Expected each time: output ending in the three `Total time (ms)` / `Nodes searched` /
`Nodes/second` lines shown above. Write down all three `Nodes searched` and `Nodes/second`
values (e.g. in a scratch note) - you'll need them for Task 3.

- [ ] **Step 3: Confirm determinism of the bázis runs**

Compare the three `Nodes searched` values recorded in Step 2.
Expected: all three are **bit-for-bit identical** (this mirrors the existing
`docs/perf/nps-baseline.md` determinism check - `bench` always starts each position from a fresh
TT, so node count must not vary run to run). If they differ, STOP: something is already wrong
independent of this change (e.g. a non-deterministic search bug) - do not proceed, and report
this instead of continuing the plan.

- [ ] **Step 4: Build the "jelölt" (candidate, flag ON) binary**

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHESS_NATIVE_ARCH=ON
cmake --build build --config Release --target chess_engine
```
Expected: build succeeds, exit code 0.

- [ ] **Step 5: Run bench 3 times on the jelölt binary, record output**

Run three times: `build/Release/chess_engine.exe bench 8`
Expected: same output shape as Step 2. Record all three `Nodes searched` and `Nodes/second`
values.

- [ ] **Step 6: Compare Nodes searched between bázis and jelölt**

Compare the (identical) bázis `Nodes searched` value from Step 3 against the (identical) jelölt
`Nodes searched` value from Step 5.
Expected: **the two values are bit-for-bit identical.** This proves the search tree didn't
change - only execution speed did.
If they differ: STOP. This is not a pure speed change under `CLAUDE.md`'s protocol. Revert
Task 1's `CMakeLists.txt` change (`git revert <task-1-commit>`) and report the discrepancy
instead of proceeding to Task 3.

- [ ] **Step 7: Compare median NPS between bázis and jelölt**

Sort each group of 3 `Nodes/second` values and take the middle (median) one.
Expected: the jelölt (ON) median NPS is **measurably higher** than the bázis (OFF) median NPS.
If it is not (e.g. within noise, or lower): STOP. Revert Task 1's `CMakeLists.txt` change and
report that the flag did not earn its keep, instead of proceeding to Task 3.

(No commit for this task - it produces measurements, not file changes. Keep your recorded
numbers from Steps 2 and 5 for Task 3.)

---

### Task 3: Update documentation with corrected claims and measured results

**Files:**
- Modify: `docs/perf/nps-baseline.md`
- Modify: `README.md`
- Modify: `CLAUDE.md`

**Interfaces:**
- Consumes: the 3+3 bench measurements recorded in Task 2 (Steps 2 and 5).

- [ ] **Step 1: Correct the MSVC baseline's inaccurate "Megjegyzés" in `docs/perf/nps-baseline.md`**

Replace:

```markdown
- **Megjegyzés:** a `CHESS_NATIVE_ARCH` / `CHESS_LTO` CMake-kapcsolók
  `NOT MSVC`-re vannak őrizve, tehát ezen a buildend **no-op**-ok — ez a
  baseline a sima MSVC `/O2` teljesítményét tükrözi, nem a native-arch/LTO
  hatását. A -march=native/LTO hatását a GCC/Clang oldalon (pl. Raspberry Pi)
  kell mérni.
```

with:

```markdown
- **Megjegyzés:** ez a baseline a `CHESS_NATIVE_ARCH` MSVC-ági bevezetése
  **előtti** állapotot tükrözi. A `CHESS_LTO` valójában már ekkor is aktív
  volt MSVC-n (a generált `.vcxproj`-ban `WholeProgramOptimization=true`,
  ami linkeléskor implicit `/LTCG`-t is jelent) — csak a `CHESS_NATIVE_ARCH`
  volt ténylegesen no-op (nem volt MSVC-ága). Lásd lejjebb az "MSVC +
  /arch:AVX2" szakaszt a `CHESS_NATIVE_ARCH` MSVC-ági hatásának mérésére.
```

- [ ] **Step 2: Add a new measured section to `docs/perf/nps-baseline.md`**

Insert a new section immediately after the existing "## Baseline: Windows / MSVC" section's
tables (i.e. right before the "## Raspberry Pi / GCC baseline" heading). Fill the table with
your own three OFF-run and three ON-run values recorded in Task 2 Steps 2 and 5 (this is not a
placeholder to skip - use the exact numbers you measured), following this structure:

```markdown
## MSVC + /arch:AVX2 (CHESS_NATIVE_ARCH hatása)

A `CHESS_NATIVE_ARCH` mostantól MSVC-n is hatással van: bekapcsolva `/arch:AVX2`-t ad a
fordítási flagekhez (lásd `CMakeLists.txt`). Az alábbi mérés ugyanazon a gépen és commit-on
készült, mint a fenti Windows/MSVC baseline, kizárólag a `CHESS_NATIVE_ARCH` be- (jelölt) és
kikapcsolásával (bázis) — a különbség így kizárólag ennek a kapcsolónak tudható be.

### Node-count paritás (depth 8, 3-3 futás)

Bázis (`CHESS_NATIVE_ARCH=OFF`), mindhárom futás: `Nodes searched : <bázis node count>`
Jelölt (`CHESS_NATIVE_ARCH=ON`), mindhárom futás: `Nodes searched : <jelölt node count>`

A két érték bitre azonos — a keresési fa nem változott.

### Depth 8 NPS összehasonlítás (3-3 futás)

| Konfiguráció | Futás | Idő (ms) | Nodes | NPS |
|---|---|---|---|---|
| bázis (OFF) | 1 | <ms> | <nodes> | <nps> |
| bázis (OFF) | 2 | <ms> | <nodes> | <nps> |
| bázis (OFF) | 3 | <ms> | <nodes> | <nps> |
| jelölt (ON) | 1 | <ms> | <nodes> | <nps> |
| jelölt (ON) | 2 | <ms> | <nodes> | <nps> |
| jelölt (ON) | 3 | <ms> | <nodes> | <nps> |

**Medián NPS, bázis (OFF):** <medián>
**Medián NPS, jelölt (ON, /arch:AVX2):** <medián>
```

- [ ] **Step 3: Correct `README.md`**

Replace:

```markdown
On GCC/Clang, the `CHESS_NATIVE_ARCH` and `CHESS_LTO` CMake options (both default `ON`) enable
`-march=native`/`-mcpu=native` and link-time optimization for extra speed; both are no-ops under
MSVC.
```

with:

```markdown
The `CHESS_NATIVE_ARCH` and `CHESS_LTO` CMake options (both default `ON`) enable extra
compile-time speed tuning. On GCC/Clang, `CHESS_NATIVE_ARCH` adds `-march=native`/`-mcpu=native`;
on MSVC it adds a fixed `/arch:AVX2` instead (MSVC has no host-detecting "native" flag).
`CHESS_LTO` enables link-time/interprocedural optimization on both toolchains (on MSVC,
`/GL` + `/LTCG`).
```

- [ ] **Step 4: Correct `CLAUDE.md`**

Replace (in the "Build, lint, teszt" section):

```markdown
- A `CHESS_NATIVE_ARCH` és `CHESS_LTO` CMake opciók (alapból ON, GCC/Clang-en) `-march=native`/
  `-mcpu=native`-et, illetve LTO-t kapcsolnak be sebesség céljából; MSVC-n mindkettő no-op.
```

with:

```markdown
- A `CHESS_NATIVE_ARCH` és `CHESS_LTO` CMake opciók (alapból ON) sebesség céljából kapcsolnak be
  extra fordítási optimalizációt: GCC/Clang-en `-march=native`/`-mcpu=native`-et, illetve LTO-t;
  MSVC-n `CHESS_NATIVE_ARCH` egy rögzített `/arch:AVX2`-t ad (MSVC-nek nincs host-detektáló
  "native" flagje), `CHESS_LTO` pedig `/GL`+`/LTCG`-t.
```

- [ ] **Step 5: Review the diff before committing**

Run: `git diff docs/perf/nps-baseline.md README.md CLAUDE.md`
Expected: only the intended prose/table changes from Steps 1-4 appear - no accidental
whitespace or unrelated edits.

- [ ] **Step 6: Commit**

```bash
git add docs/perf/nps-baseline.md README.md CLAUDE.md
git commit -m "docs: correct MSVC LTO claim and record CHESS_NATIVE_ARCH AVX2 baseline"
```

---

## Self-Review Notes

- **Spec coverage:** Task 1 implements the design's "Terv" item 1 (CMakeLists.txt change).
  Task 2 implements item 2 (validation per the bench protocol, including the explicit
  node-count-mismatch and no-improvement abort paths from the design's "Nem cél" scoping).
  Task 3 implements item 3 (nps-baseline.md, README.md correction) plus CLAUDE.md, which the
  design didn't explicitly list but which contains the identical inaccurate claim and is
  checked-in project guidance - fixing it keeps all three docs consistent.
- **Placeholder scan:** the `<...>` markers in Task 3 Step 2's table are explicitly called out
  as "not a placeholder to skip" and are filled from Task 2's own measured output, not left
  vague - this is data-dependent content, not a deferred decision.
- **Type/name consistency:** `CHESS_HAS_ARCH_AVX2` (Task 1) is a local CMake variable used only
  within Task 1's own step, not referenced elsewhere. `CHESS_TUNING_COMPILE_OPTIONS` is the
  existing, unmodified list name already consumed by `target_compile_options()` calls later in
  `CMakeLists.txt` (outside this plan's scope).
