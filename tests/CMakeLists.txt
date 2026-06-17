# tests/CMakeLists.txt

function(pi_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            pi_core
            pi_http
            pi_ai
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endfunction()

pi_add_test(test_core)
pi_add_test(test_http)
pi_add_test(test_sse)
pi_add_test(test_anthropic_parser)
pi_add_test(test_models)
