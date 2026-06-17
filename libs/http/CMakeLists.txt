# libs/http/CMakeLists.txt

add_library(pi_http STATIC
    src/http_types.cpp
    src/http_client.cpp
    src/sse_parser.cpp
)

target_include_directories(pi_http
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pi_http
    PUBLIC
        pi_core
        OpenSSL::SSL
        OpenSSL::Crypto
)

if(UNIX)
    target_link_libraries(pi_http PUBLIC pthread)
endif()
