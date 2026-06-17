# libs/agent/CMakeLists.txt (placeholder for Phase 2)
add_library(pi_agent INTERFACE)
target_include_directories(pi_agent INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pi_agent INTERFACE pi_core pi_ai)
