# MSVC natív architektúra-célzás (`/arch:AVX2`)

## Cél

A `CHESS_NATIVE_ARCH` CMake-opció ("Compile for the host CPU") jelenleg csak GCC/Clang alatt
fejt ki hatást (`-march=native`/`-mcpu=native`); MSVC-n no-op. Ez a terv ehhez az opcióhoz ad
egy MSVC-ágat, hogy a Windows/MSVC build is részesüljön abból a sebességi nyereségből, amit a
GCC/Clang oldal már élvez — kifejezetten a `docs/perf/nps-baseline.md`-ben rögzített protokoll
szerinti, tisztán sebességi (nem keresési fát érintő) változtatásként.

## Kontextus / feltárt tények

- A `CMakeLists.txt`-ben `CHESS_NATIVE_ARCH` blokkja `if(CHESS_NATIVE_ARCH AND NOT MSVC)` alá
  van zárva — MSVC-n ez valóban no-op ma.
- A `CHESS_LTO` blokk **nincs** `NOT MSVC`-re őrizve. A meglévő build (`build/chess_lib.vcxproj`,
  `build/chess_engine.vcxproj`) ellenőrzése megerősítette, hogy a Release konfigurációban
  `WholeProgramOptimization=true` már ténylegesen be van kapcsolva MSVC-n (ez MSBuild-on
  implicit `/LTCG`-t is jelent linkeléskor). Tehát az LTO/IPO **már aktív** volt a jelenlegi
  `nps-baseline.md` mérésekor is.
- Ebből következően a `docs/perf/nps-baseline.md` és a `README.md` azon állítása, hogy „mindkét
  kapcsoló no-op MSVC-n", **pontatlan**: csak a natív-architektúra célzás hiányzik ténylegesen.
  Ezt a jelen munka részeként javítjuk.
- A meglévő GCC/Clang-ági komment (`CMakeLists.txt`) szerint az arch-célzás létjogosultsága,
  hogy `std::popcount`/`std::countr_zero` (`src/bitboard.hpp`) csak megfelelő ISA-célzás mellett
  fordul le hardveres POPCNT/TZCNT/BMI utasításra, különben szoftveres fallbackre. Ugyanez a
  motiváció áll fenn MSVC oldalon is.

## Terv

1. **`CMakeLists.txt`**: a meglévő `if(CHESS_NATIVE_ARCH AND NOT MSVC)` blokk mellé egy új
   `elseif(CHESS_NATIVE_ARCH AND MSVC)` ág (vagy ezzel egyenértékű szerkezet), amely — a meglévő
   GCC/Clang ág mintáját követve — `check_cxx_compiler_flag`-gel leellenőrzi, hogy a fordító
   elfogadja-e a `/arch:AVX2` kapcsolót, és ha igen, felveszi a `CHESS_TUNING_COMPILE_OPTIONS`
   listába. Ha a flag valamiért nem támogatott (pl. szokatlan toolset), a build csendben, flag
   nélkül fut tovább — nem törik el.
   - Nincs futásidejű CPU-detektálás: a `/arch:AVX2` egy rögzített, mindig ugyanaz az
     alapértelmezés MSVC Release build-eken, amíg `CHESS_NATIVE_ARCH` ON (a jelenlegi
     alapértelmezés). Aki más hardverre disztribúcióhoz fordít, továbbra is kikapcsolhatja
     `-DCHESS_NATIVE_ARCH=OFF`-fal, ugyanúgy, mint ma a GCC/Clang ágon.
   - A `CHESS_LTO` blokkot nem módosítjuk — az már helyesen működik MSVC-n.
2. **Validálás** a CLAUDE.md sebesség-protokollja szerint:
   - Bázis: jelenlegi MSVC Release build (`/arch:AVX2` nélkül).
   - Jelölt: ugyanaz, `/arch:AVX2`-vel.
   - `chess_engine bench <fix depth>` (depth 8, a gyors iterációhoz ajánlott mélység a
     `nps-baseline.md` szerint) mindkét fán, **legalább 3 egymást követő futással**.
   - Megtartás feltétele: **bitre azonos "Nodes searched"** mindkét oldalon, **és** a jelölt
     médián NPS-e mérhetően magasabb, mint a bázisé.
   - Ha a node-count eltérne (nem várt, mivel tisztán fordítói optimalizációról van szó, nem
     algoritmusváltásról), a változtatás nem tartható meg sebességi alapon — ez esetben vissza
     kell vonni és az SPRT-protokoll alá kellene sorolni, de ez a terv nem számít erre.
3. **Dokumentáció frissítése**:
   - `docs/perf/nps-baseline.md`: a „mindkét kapcsoló no-op MSVC-n" mondat pontosítása (csak a
     natív-arch célzás volt az, az LTO nem), és egy új mért baseline felvétele a
     `/arch:AVX2`-vel futó MSVC build-ről, a meglévő táblázatok formátumát követve (determinizmus,
     depth 8, adott esetben depth 12).
   - `README.md`: a "both are no-ops under MSVC" mondat javítása a tényleges állapotra (LTO már
     korábban is aktív volt; natív-arch célzás mostantól AVX2-re rögzítve aktív MSVC-n is).

## Nem cél / határok

- Nincs futásidejű/CPUID-alapú `/arch:` detektálás (ezt a felhasználó kifejezetten elvetette az
  automatikus-detektálás javára a rögzített AVX2 mellett).
- Nem érinti a keresési fát, a move ordering-et vagy bármilyen keresési/eval döntést — tisztán
  fordítói kódgenerálási változtatás, így nem esik az SPRT-szabály alá (CLAUDE.md).
- Nem foglalkozik a korábban azonosított, jóval nagyobb hatású lehetőséggel (legális
  lépésgenerálás make/unmake nélkül, `movegen.cpp`) — azt a felhasználó kifejezetten nem
  választotta ehhez a körhöz; külön brainstorming/spec témája lehet később.
- Nem foglalkozik a Raspberry Pi / GCC-Clang oldali méréssel (az már a meglévő
  `CHESS_NATIVE_ARCH`/`CHESS_LTO` logikával lefedett, változatlan).
