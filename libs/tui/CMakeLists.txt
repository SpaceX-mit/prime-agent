# libs/tui/CMakeLists.txt (placeholder for Phase 3)
add_library(pi_tui INTERFACE)
target_include_directories(pi_tui INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pi_tui INTERFACE pi_core)
