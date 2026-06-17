# libs/agent/CMakeLists.txt
add_library(pi_agent STATIC
    src/agent_loop.cpp
)

target_include_directories(pi_agent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pi_agent
    PUBLIC
        pi_core
        pi_ai
)

if(UNIX)
    target_link_libraries(pi_agent PUBLIC pthread)
endif()
