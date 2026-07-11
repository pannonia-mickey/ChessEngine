# SPRT tool

Validate whether an engine change is a real strength improvement (per the
project rule: keep a change only if SPRT proves a gain; revert on regression
or statistical wash). Works on Windows (Git Bash) and Linux/Raspberry Pi.

## One-time setup

```bash
tools/sprt/setup.sh
```

Obtains [fastchess](https://github.com/Disservin/fastchess) into
`tools/sprt/fastchess/` (prebuilt `.exe` on Windows, built from source on
Linux/ARM; or reuses one on `PATH`) and downloads the `8moves_v3` opening book
into `tools/sprt/books/`. Both are gitignored.

## Running a test

Commit your baseline, make your change, then compare your working tree against
the baseline commit:

```bash
tools/sprt/run_sprt.sh HEAD             # candidate = working tree, base = HEAD
tools/sprt/run_sprt.sh main my-branch   # two explicit git refs
```

The candidate is engine 1, so a **positive Elo** means the candidate is
stronger. fastchess stops early when the LLR crosses a bound:
- **H1 accepted** → candidate is stronger; keep the change.
- **H0 accepted** → no gain or a regression; revert the change.

## Tuning (environment variables)

| Var | Default | Meaning |
|---|---|---|
| `SPRT_TC` | `10+0.1` | Time control: seconds + increment per game. |
| `SPRT_ELO0` / `SPRT_ELO1` | `0` / `5` | SPRT hypothesis bounds (Elo). |
| `SPRT_ALPHA` / `SPRT_BETA` | `0.05` / `0.05` | Type-I / type-II error rates. |
| `SPRT_ROUNDS` | `4000` | Max rounds (2 games each) before giving up. |
| `SPRT_CONCURRENCY` | `2` | Concurrent games. |
| `SPRT_TIMEMARGIN` | `1000` | ms grace before fastchess flags a time loss. |

Notes: the engine enforces its move budget on wall-clock and aborts mid-search,
so it will not forfeit on time under concurrency. The engine is deterministic,
so the opening book is what makes games differ. Very fast time controls can
mislead depth-sensitive changes — prefer a longer `SPRT_TC` when a change's
benefit is depth-dependent.
