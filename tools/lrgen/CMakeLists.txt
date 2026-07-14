cmake_minimum_required(VERSION 3.20)
project(lrgen CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Canonical LR(1) parser generator. Standalone host tool: reads a .grammar and
# emits a complete parser (<out_base>.hpp / .cpp). No third-party dependencies.
add_executable(lrgen
    main.cpp
    GrammarParser.cpp
    Lr.cpp
    Emitter.cpp
)
target_include_directories(lrgen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
