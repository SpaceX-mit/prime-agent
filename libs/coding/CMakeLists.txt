# libs/coding/CMakeLists.txt (placeholder for Phase 4)
add_library(pi_coding INTERFACE)
target_include_directories(pi_coding INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pi_coding INTERFACE pi_core pi_ai)
