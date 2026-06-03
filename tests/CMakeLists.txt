add_executable(md_tests
    test_prescanner.cpp
    test_spine.cpp
    test_inline.cpp
    test_spec.cpp
)
target_link_libraries(md_tests PRIVATE md_parser GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(md_tests)

add_subdirectory(spec_tests)
