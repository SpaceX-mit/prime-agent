# apps/pi/CMakeLists.txt
add_executable(pi main.cpp)
target_link_libraries(pi
    PRIVATE
        pi_core
        pi_http
        pi_ai
        pi_agent
        pi_coding
)
set_target_properties(pi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/pi"
)
