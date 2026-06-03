cmake_minimum_required(VERSION 3.20)
project(markdown_parser CXX)
set(CMAKE_CXX_STANDARD 20)

add_library(md_parser
    src/PreScanner.cpp
    src/SpineHandler.cpp
    src/InlineParser.cpp
)
target_include_directories(md_parser PUBLIC include)
target_include_directories(md_parser PRIVATE third_party)

add_executable(md_parser_bin src/main.cpp)
target_link_libraries(md_parser_bin PRIVATE md_parser)

add_subdirectory(third_party/googletest)
add_subdirectory(tests)
