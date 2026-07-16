add_executable(md_tests
    test_prescanner.cpp
    test_spine.cpp
    test_inline.cpp
    test_spec.cpp
)
target_link_libraries(md_tests PRIVATE md_parser GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(md_tests)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

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

add_subdirectory(spec_tests)
