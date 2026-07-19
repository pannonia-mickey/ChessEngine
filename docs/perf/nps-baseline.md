# NPS Baseline

Ez a dokumentum a motor nyers keresési sebességének (NPS) reprodukálható
alapmérését rögzíti, a `bench` UCI parancs (`src/bench.cpp`) segítségével.
Későbbi, tisztán sebességi célú változtatások (nem keresési döntést,
csak futásidőt érintők) ehhez a számhoz viszonyítva igazolhatók.

## Mérési protokoll

Egy változtatás akkor tekinthető **tisztán sebességinek** (compiler flag,
adatstruktúra-tweak, allokáció-csökkentés stb.), és akkor tartható meg
SPRT nélkül, ha `./build/chess_engine bench <fix depth>` mindkét fán:

1. **azonos "Nodes searched" értéket ad** (bitre pontosan) — ez bizonyítja,
   hogy a keresési fa (döntések, pruning, move ordering) nem változott;
2. **mérhetően magasabb NPS-t ad** — több (≥3) egymást követő futás
   mediánja alapján, a futásonkénti zajszint felett.

Ha a node-count eltér a base és a candidate között, a változtatás **nem**
tisztán sebességi, hanem keresési/algoritmikus jellegű — az az erő (SPRT)
protokollja alá tartozik, nem ez alá.

A `bench` parancs minden pozíciót friss (üres) transzpozíciós táblával
indít, így a node-count független a pozíciók sorrendjétől és egymás közti
hatásától — ez teszi reprodukálhatóvá.

## Baseline: Windows / MSVC

- **Dátum:** 2026-07-19
- **Gép:** Intel(R) Core(TM) i7-10750H CPU @ 2.60GHz, Windows 11 Home 10.0.26200 (x64)
- **Fordító/generátor:** MSVC (Visual Studio 18 2026), CMake 4.3.1
- **Build:** `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`
- **Build flagek:** `/O2 /Ob2 /DNDEBUG` (`CMAKE_CXX_FLAGS_RELEASE`)
- **Megjegyzés:** ez a baseline a `CHESS_NATIVE_ARCH` MSVC-ági bevezetése
  **előtti** állapotot tükrözi. A `CHESS_LTO` valójában már ekkor is aktív
  volt MSVC-n (a generált `.vcxproj`-ban `WholeProgramOptimization=true`,
  ami linkeléskor implicit `/LTCG`-t is jelent) — csak a `CHESS_NATIVE_ARCH`
  volt ténylegesen no-op (nem volt MSVC-ága). Lásd lejjebb az "MSVC +
  /arch:AVX2" szakaszt a `CHESS_NATIVE_ARCH` MSVC-ági hatásának mérésére.
- **Commit:** 56c88fb (perf: add NPS bench command, lazy move ordering, and native/LTO build tuning)

### Determinizmus (node count, 5 egymást követő futás, depth 8)

Mind az 5 futás: `Nodes searched : 3360781` (bitre azonos).

### Depth 8 (5 futás)

| Futás | Idő (ms) | Nodes | NPS |
|---|---|---|---|
| 1 | 5245 | 3360781 | 640759 |
| 2 | 5827 | 3360781 | 576760 |
| 3 | 5060 | 3360781 | 664185 |
| 4 | 5285 | 3360781 | 635909 |
| 5 | 5065 | 3360781 | 663530 |

**Medián NPS (depth 8): ~640 759**

### Depth 12 (BENCH_DEFAULT_DEPTH; 2 futás — lassabb, gyors iterációhoz depth 8 ajánlott)

Mindkét futás: `Nodes searched : 88602459` (bitre azonos).

| Futás | Idő (ms) | Nodes | NPS |
|---|---|---|---|
| 1 | 139268 | 88602459 | 636201 |
| 2 | 139837 | 88602459 | 633612 |

## MSVC + /arch:AVX2 (CHESS_NATIVE_ARCH hatása)

A `CHESS_NATIVE_ARCH` mostantól MSVC-n is hatással van: bekapcsolva `/arch:AVX2`-t ad a
fordítási flagekhez (lásd `CMakeLists.txt`). Az alábbi mérés ugyanazon a gépen és commit-on
készült, mint a fenti Windows/MSVC baseline, kizárólag a `CHESS_NATIVE_ARCH` be- (jelölt) és
kikapcsolásával (bázis) — a különbség így kizárólag ennek a kapcsolónak tudható be.

### Node-count paritás (depth 8, 3-3 futás)

Bázis (`CHESS_NATIVE_ARCH=OFF`), mindhárom futás: `Nodes searched : 3360781`
Jelölt (`CHESS_NATIVE_ARCH=ON`), mindhárom futás: `Nodes searched : 3360781`

A két érték bitre azonos — a keresési fa nem változott.

### Depth 8 NPS összehasonlítás (3-3 futás)

| Konfiguráció | Futás | Idő (ms) | Nodes | NPS |
|---|---|---|---|---|
| bázis (OFF) | 1 | 4912 | 3360781 | 684198 |
| bázis (OFF) | 2 | 4917 | 3360781 | 683502 |
| bázis (OFF) | 3 | 4918 | 3360781 | 683363 |
| jelölt (ON) | 1 | 4561 | 3360781 | 736851 |
| jelölt (ON) | 2 | 4567 | 3360781 | 735883 |
| jelölt (ON) | 3 | 4572 | 3360781 | 735078 |

**Medián NPS, bázis (OFF):** 683502
**Medián NPS, jelölt (ON, /arch:AVX2):** 735883

## Raspberry Pi / GCC baseline

Még nincs felvéve. Ha a `CHESS_NATIVE_ARCH`/`CHESS_LTO` kapcsolók hatását
mérjük, ezt a szakaszt itt kell kitölteni ugyanezzel a protokollal
(`-march=native`/`-mcpu=native` + LTO ténylegesen aktív ezen a toolchainen).

## Fázis 1: gyors legalitás-szűrő a generate_legal-ban

Az NPS-optimalizáció 1. fázisa a lépésenkénti `do_move`/`square_attacked_by`/
`undo_move` legalitás-tesztet (`generate_legal`, `src/movegen.cpp`) egy
csomópontonként egyszer számolt checkers/pinned bitboard-alapú szűrőre
cserélte. A király-,
en passant- és sáncolás-lépések változatlanul a pontos do/undo tesztet
használják; minden más pszeudó-lépést olcsó bitboard-teszt szűr. A kibocsátott
lépéslista (halmaz és sorrend) bitre azonos maradt — ezt a
`tests/test_perft.cpp`-beli "fast generate_legal matches do/undo reference"
regressziós teszt igazolja (a régi do/undo-alapú implementációt referenciaként
megtartva), a meglévő perft tesztek mellett.

Ugyanazon a gépen mérve, mint a fenti "MSVC + /arch:AVX2" szakasz (baseline
onnan: medián NPS 735883, `CHESS_NATIVE_ARCH=ON`).

### Node-count paritás

Depth 8, 4 futás: mind a 4: `Nodes searched : 3360781` (bitre azonos a
baseline-nal).
Depth 12, 1 futás: `Nodes searched : 88602459` (bitre azonos a baseline-nal).

### Depth 8 NPS (4 futás)

| Futás | Idő (ms) | Nodes | NPS |
|---|---|---|---|
| 1 | 2175 | 3360781 | 1545186 |
| 2 | 2177 | 3360781 | 1543767 |
| 3 | 2180 | 3360781 | 1541642 |
| 4 | 2178 | 3360781 | 1543058 |

**Medián NPS (depth 8): ~1 543 767** (bázis: 735883 → **~2.1x**)

### Depth 12 (1 futás, megerősítésként)

Idő: 57645 ms, NPS: 1537036 (bázis: 636201-633612 → **~2.4x**)
