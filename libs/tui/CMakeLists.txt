# libs/tui/CMakeLists.txt
add_library(pi_tui STATIC
    src/terminal.cpp
    src/tui.cpp
    src/theme.cpp
    src/render_util.cpp
    src/message_render.cpp
    src/think_filter.cpp
    src/components/input.cpp
    src/components/editor.cpp
    src/components/footer.cpp
    src/modes/interactive.cpp
)

target_include_directories(pi_tui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pi_tui
    PUBLIC
        pi_core
        pi_ai
        pi_agent
        pi_coding
        pi_http
)

if(UNIX)
    target_link_libraries(pi_tui PUBLIC pthread)
endif()
