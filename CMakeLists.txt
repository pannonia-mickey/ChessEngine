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

# Core engine sources (added to as tasks progress).
set(ENGINE_SOURCES
  src/bitboard.cpp
  src/attacks.cpp
  src/position.cpp
  src/movegen.cpp
  src/perft.cpp
  src/eval.cpp
  src/search.cpp
  src/zobrist.cpp
  src/tt.cpp
  src/uci.cpp
)

find_package(Threads REQUIRED)

add_library(chess_lib STATIC ${ENGINE_SOURCES})
target_include_directories(chess_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
# STATIC lib with no sources yet is invalid; guard until first source is added:
set_target_properties(chess_lib PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(chess_lib PUBLIC Threads::Threads)

add_executable(chess_engine src/main.cpp)
target_link_libraries(chess_engine PRIVATE chess_lib)

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
  add_executable(chess_tests tests/test_main.cpp tests/test_types.cpp tests/test_bitboard.cpp tests/test_attacks.cpp tests/test_position.cpp tests/test_movegen.cpp tests/test_perft.cpp tests/test_eval.cpp tests/test_search.cpp tests/test_zobrist.cpp tests/test_tt.cpp tests/test_uci.cpp)
  target_link_libraries(chess_tests PRIVATE chess_lib doctest::doctest)
  target_include_directories(chess_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
  add_test(NAME unit COMMAND chess_tests)
endif()
