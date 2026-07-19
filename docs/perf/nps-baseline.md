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
- **Megjegyzés:** a `CHESS_NATIVE_ARCH` / `CHESS_LTO` CMake-kapcsolók
  `NOT MSVC`-re vannak őrizve, tehát ezen a buildend **no-op**-ok — ez a
  baseline a sima MSVC `/O2` teljesítményét tükrözi, nem a native-arch/LTO
  hatását. A -march=native/LTO hatását a GCC/Clang oldalon (pl. Raspberry Pi)
  kell mérni.
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

## Raspberry Pi / GCC baseline

Még nincs felvéve. Ha a `CHESS_NATIVE_ARCH`/`CHESS_LTO` kapcsolók hatását
mérjük, ezt a szakaszt itt kell kitölteni ugyanezzel a protokollal
(`-march=native`/`-mcpu=native` + LTO ténylegesen aktív ezen a toolchainen).
