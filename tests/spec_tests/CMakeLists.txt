include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(spec_tests
    test_commonmark_spec.cpp
)

target_include_directories(spec_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(spec_tests PRIVATE
    md_parser
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

target_compile_definitions(spec_tests PRIVATE
    SPEC_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../spec/commonmark_spec.json"
)

include(GoogleTest)
gtest_discover_tests(spec_tests PROPERTIES LABELS "spec")
