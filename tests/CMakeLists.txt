# tests/CMakeLists.txt

function(pi_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            pi_core
            pi_http
            pi_ai
            pi_agent
            pi_coding
            pi_tui
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
pi_add_test(test_tools)
pi_add_test(test_tui)
pi_add_test(test_session)
pi_add_test(test_compaction)
pi_add_test(test_oauth)
pi_add_test(test_editor)
pi_add_test(test_streaming)
pi_add_test(test_reliability)
pi_add_test(test_render)
pi_add_test(test_context_files)
pi_add_test(test_markdown)
