include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

include(GoogleTest)

# CommonMark v0.31.2 spec conformance suite. Each spec example is loaded from
# the JSON fixture and run as an individual parameterized test case.
add_executable(markdown_tests
    markdown/test_commonmark_spec.cpp
    markdown/test_json_mdast.cpp
)
target_include_directories(markdown_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/markdown)
target_link_libraries(markdown_tests PRIVATE
    md_parser
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
target_compile_definitions(markdown_tests PRIVATE
    SPEC_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test-files/markdown/commonmark_spec.json"
    SPEC_MDAST_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test-files/markdown/commonmark_spec_mdast.json"
)
gtest_discover_tests(markdown_tests PROPERTIES LABELS "spec")

# Mermaid flowchart suite: lexer/parse/lower unit tests plus golden-file
# verification against the official mermaid library's output.
add_executable(mermaid_tests
    mermaid/test_lexer.cpp
    mermaid/test_parse.cpp
    mermaid/test_lower.cpp
    mermaid/test_flow_ast.cpp
)
target_link_libraries(mermaid_tests PRIVATE mermaid GTest::gtest_main nlohmann_json::nlohmann_json)
target_compile_definitions(mermaid_tests PRIVATE
    FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/test-files/mermaid"
)
gtest_discover_tests(mermaid_tests)
