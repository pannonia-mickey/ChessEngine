# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Status

This repository is currently empty aside from a README (`# ChessEngine`). No build tooling,
dependencies, or tests have been established yet.

## Cél

A projekt célja egy sakkmotor elkészítése a legmodernebb C++ nyelven.

## Architektúra

- **Táblaábrázolás:** bitboard.
- **Lépésgenerálás (csúszó figurák — futó, bástya, vezér):** magic bitboard technika.
- A magic numbereket a program futásidőben (runtime) számolja ki, nem előre generált/hardcode-olt
  táblázatból tölti be őket.
- **Kommunikáció:** a motor a UCI (Universal Chess Interface) protokollon kommunikál.

## Tesztelés

- A lépésgenerátor tesztelésére, validálására perft teszteket kell használni.
- A sakkmotor erősségét befolyásoló fejlesztéseket SPRT mérkőzésekkel igazolni kell. A módosítás
  csak akkor tartható meg, ha az SPRT igazolja a fejlődést. Regresszió vagy statisztikai wash
  esetén visszavonjuk.
- Ez a szabály hangolásra (eval-tagok, súlyok, keresési heurisztikák) vonatkozik, ahol a
  változtatás hatása előre nem eldöntött. **Bugfixekhez nem kell SPRT**: ha a kód nem azt csinálja,
  amit a saját szándéka (kommentek, tesztek, dokumentáció) szerint kellene, azt a hibát javítjuk és
  megtartjuk, az SPRT eredményétől függetlenül — elég hozzá egy reprodukáló regressziós teszt.
- A méréshez a `tools/sprt/` eszközt használd: előbb `tools/sprt/setup.sh`
  (egyszeri), majd `tools/sprt/run_sprt.sh <base_ref> [candidate_ref]`. Az
  eszköz fastchess segítségével játszik SPRT mérkőzést a bázis és a jelölt
  build között, nyitókönyvvel; a jelölt csak akkor tartható meg, ha „H1
  accepted” (elfogadott erőnövekedés) születik. Windowson (Git Bash) és
  Linux/Raspberry Pi-n egyaránt fut.

## Guidance for Future Work

- Once the build tooling and structure are set up, update this file with:
  - Build, lint, and test commands (including how to run a single test)
  - Further architecture notes (module boundaries, key data flows, design decisions)
- Until then, confirm with the user before scaffolding project structure beyond what's specified above.
