# libs/coding/CMakeLists.txt
add_library(pi_coding STATIC
    src/tools/bash.cpp
    src/tools/read.cpp
    src/tools/write.cpp
    src/tools/edit.cpp
)

target_include_directories(pi_coding
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pi_coding
    PUBLIC
        pi_core
        pi_ai
        pi_agent
)

if(UNIX)
    target_link_libraries(pi_coding PUBLIC pthread)
endif()
