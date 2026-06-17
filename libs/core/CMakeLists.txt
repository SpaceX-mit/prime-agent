# libs/core/CMakeLists.txt

add_library(pi_core STATIC
    src/env.cpp
    src/file_io.cpp
    src/lockfile.cpp
    src/log.cpp
    src/path.cpp
    src/unicode_width.cpp
)

target_include_directories(pi_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pi_core
    PUBLIC
        pi::thirdparty
)

if(UNIX AND NOT APPLE)
    target_link_libraries(pi_core PUBLIC rt)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(pi_core PUBLIC pthread)
endif()
