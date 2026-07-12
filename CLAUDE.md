# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Cél

A projekt célja egy sakkmotor elkészítése a legmodernebb C++ nyelven.

## Build, lint, teszt

- **Konfigurálás + build:** `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
  (C++23, CMake ≥ 3.20; a doctest keretrendszert a CMake `FetchContent`-en keresztül tölti le).
- **Motor futtatása (UCI):** `./build/chess_engine`.
- **Tesztek futtatása:** `ctest --test-dir build --output-on-failure`, vagy közvetlenül
  `./build/chess_tests`.
- **Egy teszt futtatása:** `./build/chess_tests --test-case="<TEST_CASE neve>"` (doctest szűrő).
- A `CHESS_BUILD_TESTS` CMake opció (alapból ON) kapcsolja a `chess_tests` targetet.
- Warningok: MSVC-n `/W4`, egyébként `-Wall -Wextra` (lásd `CMakeLists.txt`).

## Architektúra

- **Táblaábrázolás:** bitboard (`src/bitboard.hpp/.cpp`, LERF square indexelés).
- **Lépésgenerálás (csúszó figurák — futó, bástya, vezér):** magic bitboard technika
  (`src/attacks.hpp/.cpp`); a nem csúszó figurák (gyalog, huszár, király) statikus attack
  táblákból dolgoznak.
- A magic numbereket a program futásidőben (runtime) számolja ki, az `attacks::init()`-ben, nem
  előre generált/hardcode-olt táblázatból tölti be őket.
- **Pozíció (`src/position.hpp/.cpp`):** bitboard-ok + 64 elemű tömb, Zobrist kulcs
  (`src/zobrist.hpp/.cpp`), FEN import/export, `do_move`/`undo_move`, valamint külön
  null-move alkalmazás/visszavonás a null-move pruninghoz.
- **Keresés (`src/search.hpp/.cpp`):** iteratív mélyítésű negamax alfa-béta vágással,
  quiescence kereséssel, transzpozíciós táblával (`src/tt.hpp/.cpp`), null-move pruninggal,
  late move reduction-nel (LMR), MultiPV támogatással, és `stop`/movetime/node-limit alapján
  megszakítható kereséssel.
- **Értékelés (`src/eval.cpp`):** anyagérték + piece-square táblák (PST), a lépő fél
  szemszögéből, centipawnban.
- **Kommunikáció:** a motor a UCI (Universal Chess Interface) protokollon kommunikál
  (`src/uci.hpp/.cpp`): `position`, `go` (depth/movetime/wtime-btime/nodes/searchmoves/infinite/
  ponder), `stop`, `ponderhit`, `setoption` (Hash, Ponder, MultiPV), `debug`, `ucinewgame`.

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
