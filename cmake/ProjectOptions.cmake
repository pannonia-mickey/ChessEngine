# Shared compile settings for every first-party target.
# Third-party code (e.g. Catch2) does not link this, so our warning flags never apply to it.

add_library(chess_project_options INTERFACE)
add_library(chess::project_options ALIAS chess_project_options)

target_compile_features(chess_project_options INTERFACE cxx_std_23)

if(MSVC)
  target_compile_options(chess_project_options INTERFACE /W4 /permissive- $<$<BOOL:${CHESS_WARNINGS_AS_ERRORS}>:/WX>)
else()
  target_compile_options(
    chess_project_options
    INTERFACE -Wall
              -Wextra
              -Wpedantic
              -Wshadow
              -Wconversion
              -Wsign-conversion
              -Wold-style-cast
              -Wcast-align
              -Wnon-virtual-dtor
              -Woverloaded-virtual
              -Wnull-dereference
              -Wdouble-promotion
              -Wimplicit-fallthrough
              -Wformat=2
              $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-cond>
              $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-branches>
              $<$<CXX_COMPILER_ID:GNU>:-Wlogical-op>
              $<$<CXX_COMPILER_ID:GNU>:-Wuseless-cast>
              $<$<BOOL:${CHESS_WARNINGS_AS_ERRORS}>:-Werror>)

  if(CHESS_ENABLE_SANITIZERS)
    set(_chess_sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(chess_project_options INTERFACE ${_chess_sanitizer_flags})
    target_link_options(chess_project_options INTERFACE ${_chess_sanitizer_flags})
  endif()
endif()

if(CHESS_ENABLE_CLANG_TIDY)
  find_program(CHESS_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
  set(CHESS_CLANG_TIDY_COMMAND "${CHESS_CLANG_TIDY_EXE}")
  if(CHESS_WARNINGS_AS_ERRORS)
    list(APPEND CHESS_CLANG_TIDY_COMMAND "--warnings-as-errors=*")
  endif()
endif()

# Enables clang-tidy on a first-party target when CHESS_ENABLE_CLANG_TIDY is set.
function(chess_enable_clang_tidy target)
  if(CHESS_ENABLE_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CHESS_CLANG_TIDY_COMMAND}")
  endif()
endfunction()
