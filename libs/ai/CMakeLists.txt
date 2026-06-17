# libs/ai/CMakeLists.txt

add_library(pi_ai STATIC
    src/event_stream.cpp
    src/provider.cpp
    src/models.cpp
    src/stream_simple.cpp
    src/providers/anthropic.cpp
    src/providers/openai.cpp
    src/providers/google.cpp
    src/providers/mistral.cpp
    src/register_providers.cpp
)

target_include_directories(pi_ai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pi_ai
    PUBLIC
        pi_core
        pi_http
)

if(UNIX)
    target_link_libraries(pi_ai PUBLIC pthread)
endif()
