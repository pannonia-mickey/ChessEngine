# fastchess (https://github.com/Disservin/fastchess), the match runner tools/sprt.py plays SPRT tests
# with (see docs/sprt.md). It is fetched at a pinned release and built with its own Makefile, since
# it has no CMake build; the binary ends up at
# build/<preset>/tools/fastchess/bin/fastchess, where tools/sprt.py finds it. Build it alone
# with `cmake --build --preset release --target fastchess`.
include(ExternalProject)

set(CHESS_FASTCHESS_TAG
    "v1.8.2-alpha"
    CACHE STRING "fastchess git tag (or commit) to build")

find_program(CHESS_MAKE_PROGRAM NAMES gmake make REQUIRED)
cmake_host_system_information(RESULT cores QUERY NUMBER_OF_LOGICAL_CORES)

set(fastchess_binary "${CMAKE_CURRENT_BINARY_DIR}/bin/fastchess${CMAKE_EXECUTABLE_SUFFIX}")

ExternalProject_Add(
  fastchess
  GIT_REPOSITORY https://github.com/Disservin/fastchess.git
  GIT_TAG ${CHESS_FASTCHESS_TAG}
  GIT_SHALLOW TRUE
  GIT_PROGRESS FALSE
  PREFIX "${CMAKE_CURRENT_BINARY_DIR}/fastchess-build"
  UPDATE_DISCONNECTED TRUE
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${CHESS_MAKE_PROGRAM} -j${cores} CXX=${CMAKE_CXX_COMPILER}
  INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/fastchess${CMAKE_EXECUTABLE_SUFFIX}
                  ${fastchess_binary}
  BUILD_BYPRODUCTS ${fastchess_binary}
  # Its output is long and its warnings are not ours to fix.
  LOG_BUILD TRUE
  LOG_OUTPUT_ON_FAILURE TRUE)
