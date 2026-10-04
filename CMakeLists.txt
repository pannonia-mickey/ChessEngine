cmake_minimum_required(VERSION 3.25)

project(
  ChessEngine
  VERSION 0.1.0
  DESCRIPTION "UCI chess engine written in C++23"
  LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Use a separate build directory (see CMakePresets.json).")
endif()

option(CHESS_BUILD_TESTS "Build the unit tests" ON)
option(CHESS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CHESS_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CHESS_ENABLE_CLANG_TIDY "Run clang-tidy on project sources while compiling" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ProjectOptions)

add_subdirectory(src)

if(CHESS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
