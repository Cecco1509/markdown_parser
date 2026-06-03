add_executable(md_tests
    test_prescanner.cpp
    test_spine.cpp
    test_inline.cpp
    test_spec.cpp
)
target_link_libraries(md_tests PRIVATE md_parser gtest gtest_main)
target_include_directories(md_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

include(GoogleTest)
gtest_discover_tests(md_tests)
