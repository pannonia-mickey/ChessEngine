cmake_minimum_required(VERSION 3.20)
project(ChessEngine CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()
option(CHESS_BUILD_TESTS "Build the unit test target" ON)

# NPS-oriented build tuning. Both default ON (Release builds should be fast
# out of the box) but stay switchable, since a distribution/portable build
# may need to run on hardware other than the one it was compiled on.
option(CHESS_NATIVE_ARCH "Compile for the host CPU (-march=native / -mcpu=native)" ON)
option(CHESS_LTO "Enable link-time / interprocedural optimization" ON)

set(CHESS_TUNING_COMPILE_OPTIONS "")

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

if(CHESS_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT CHESS_IPO_SUPPORTED OUTPUT CHESS_IPO_ERROR)
  if(CHESS_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
endif()

# Core engine sources (added to as tasks progress).
set(ENGINE_SOURCES
  src/bitboard.cpp
  src/attacks.cpp
  src/position.cpp
  src/movegen.cpp
  src/perft.cpp
  src/eval.cpp
  src/pawn_eval.cpp
  src/search.cpp
  src/zobrist.cpp
  src/tt.cpp
  src/uci.cpp
  src/see.cpp
  src/bench.cpp
)

find_package(Threads REQUIRED)

add_library(chess_lib STATIC ${ENGINE_SOURCES})
target_include_directories(chess_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
# STATIC lib with no sources yet is invalid; guard until first source is added:
set_target_properties(chess_lib PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(chess_lib PUBLIC Threads::Threads)
target_compile_options(chess_lib PRIVATE ${CHESS_TUNING_COMPILE_OPTIONS})

add_executable(chess_engine src/main.cpp)
target_link_libraries(chess_engine PRIVATE chess_lib)
target_compile_options(chess_engine PRIVATE ${CHESS_TUNING_COMPILE_OPTIONS})

if(CHESS_BUILD_TESTS)
  # doctest via FetchContent
  include(FetchContent)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_MakeAvailable(doctest)
  unset(CMAKE_POLICY_VERSION_MINIMUM)

  enable_testing()
  add_executable(chess_tests tests/test_main.cpp tests/test_types.cpp tests/test_bitboard.cpp tests/test_attacks.cpp tests/test_position.cpp tests/test_movegen.cpp tests/test_perft.cpp tests/test_eval.cpp tests/test_search.cpp tests/test_zobrist.cpp tests/test_tt.cpp tests/test_uci.cpp tests/test_see.cpp tests/test_bench.cpp)
  target_link_libraries(chess_tests PRIVATE chess_lib doctest::doctest)
  target_include_directories(chess_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
  target_compile_options(chess_tests PRIVATE ${CHESS_TUNING_COMPILE_OPTIONS})
  add_test(NAME unit COMMAND chess_tests)
endif()
