include(FetchContent)

# Uses an installed Catch2 3.x when available, otherwise fetches a pinned release.
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE
  SYSTEM
  FIND_PACKAGE_ARGS 3)
FetchContent_MakeAvailable(Catch2)

add_executable(chess_tests test_bitboard.cpp test_perft.cpp test_position.cpp test_uci.cpp)
target_link_libraries(chess_tests PRIVATE chess::core chess::project_options Catch2::Catch2WithMain)
chess_enable_clang_tidy(chess_tests)

if(DEFINED catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
catch_discover_tests(chess_tests)
