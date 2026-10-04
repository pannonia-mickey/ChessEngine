## Summary

<!-- What changes and why. -->

## Strength

<!--
Pick one and delete the other.

Gainers and simplifications: paste the summary.md printed by tools/sprt.py
(see docs/sprt.md), one block per run, STC first, failed runs included.

Anything that does not affect play: state that the bench node count is unchanged.
-->

### SPRT

<!-- paste tools/sprt.py summary here -->

### No functional change

Bench unchanged: <!-- output of `chessengine bench`, last line -->

## Checklist

- [ ] `cmake --workflow --preset debug` passes
- [ ] Formatted with clang-format
- [ ] Commit messages that change the bench node count end with `Bench: <nodes>`
