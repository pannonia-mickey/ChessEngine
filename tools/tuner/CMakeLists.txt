# The Texel tuner (see docs/tuning.md). A separate program: the engine binary does not contain it.
add_executable(tuner tuner.cpp)
target_link_libraries(tuner PRIVATE chess::core chess::project_options)
chess_enable_clang_tidy(tuner)
