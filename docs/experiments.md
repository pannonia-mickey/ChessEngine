# Experiments

Strength changes that were tested with SPRT (see [sprt.md](sprt.md)) and not merged, so that the
same idea is not retried blindly. Each entry gives the change, the commit it was tested at (kept in
its closed pull request), the result and what might make it work later.

## Check extensions (#20, closed)

Tested against `main` at b4136d5 (bench 121701), STC 8+0.08, gainer bounds [0, 5], fastchess with
`UHO_Lichess_4852_v1.epd`, concurrency 3. Both runs were stopped by hand once the result was clear.

### Run 1: extend every check (7e7eb27)

A move that gives check is searched one ply deeper (`depth - 1 + 1`) in every child search: the
first move's full-window search, the PVS null-window search and its re-search. Bench 170108.

```text
Games: 2480, Wins: 682, Losses: 758, Draws: 1040
Elo: -10.65 +/- 9.49, nElo: -15.36 +/- 13.67
LLR: -1.83 (-2.94, 2.94) [0.00, 5.00]
```

### Run 2: extend only checks that do not lose material (d95179e)

As run 1, but a checking move is extended only when `see_ge(pos, move, 0)` holds. Bench 162969.

```text
Games: 4958, Wins: 1398, Losses: 1466, Draws: 2094
Elo: -4.77 +/- 6.56, nElo: -7.03 +/- 9.67
LOS: 7.72 %, DrawRatio: 40.98 %, PairsRatio: 0.93
Ptnml(0-2): [142, 618, 1016, 572, 131], WL/DD Ratio: 1.25
LLR: -1.96 (-2.94, 2.94) [0.00, 5.00]
```

### Conclusion

A full-ply check extension grows the tree by 34–40% at a fixed depth (bench), and with the little
forward pruning the search does today (no late move pruning, no futility pruning of quiet moves),
the extra nodes cost more than the extension finds. Worth retrying once the search prunes more, or
in a limited form: only in PV nodes, only up to `ply < 2 * root depth`, or with a cap on the
extensions along one line.
