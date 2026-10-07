add_library(
  chess_core
  bench.cpp
  bitboard.cpp
  evaluate.cpp
  magic.cpp
  move.cpp
  movegen.cpp
  perft.cpp
  position.cpp
  search.cpp
  see.cpp
  tt.cpp
  uci.cpp)
add_library(chess::core ALIAS chess_core)
target_include_directories(chess_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(chess_core PUBLIC Threads::Threads PRIVATE chess::project_options)
target_compile_definitions(chess_core PUBLIC CHESS_ENGINE_VERSION="${PROJECT_VERSION}")
chess_enable_clang_tidy(chess_core)

add_executable(chessengine main.cpp)
target_link_libraries(chessengine PRIVATE chess::core chess::project_options)
chess_enable_clang_tidy(chessengine)
