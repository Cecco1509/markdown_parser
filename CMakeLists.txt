cmake_minimum_required(VERSION 3.20)
project(markdown_parser CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(md_parser
    src/PreScanner.cpp
    src/SpineHandler.cpp
    src/InlineParser.cpp
    src/block_rules.cpp
    src/HtmlRenderer.cpp
    src/JsonRenderer.cpp
)
target_include_directories(md_parser PUBLIC include)

add_executable(md_parser_bin src/main.cpp)
target_link_libraries(md_parser_bin PRIVATE md_parser)

enable_testing()
add_subdirectory(tests)
