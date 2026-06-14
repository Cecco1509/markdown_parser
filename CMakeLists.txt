cmake_minimum_required(VERSION 3.20)
project(markdown_parser CXX)
set(CMAKE_CXX_STANDARD 20)

if(NOT EMSCRIPTEN)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_library(md_parser
    src/ScannedLine.cpp
    src/SpineHandler.cpp
    src/InlineParser.cpp
    src/block_rules.cpp
    src/HtmlRenderer.cpp
    src/HtmlRenderDebug.cpp
    src/HtmlRendererFactory.cpp
    src/HandlerRegistry.cpp
    src/JsonRenderer.cpp
    src/entities.cpp
    src/string_utils.cpp
    src/unicode_fold.cpp
    src/handlers/MermaidHandler.cpp
    src/handlers/MathHandler.cpp
)
target_include_directories(md_parser PUBLIC include)

if(NOT EMSCRIPTEN)
    add_executable(md_parser_bin src/main.cpp)
    target_link_libraries(md_parser_bin PRIVATE md_parser)

    enable_testing()
    add_subdirectory(tests)
endif()

# WebAssembly build target (only when using Emscripten)
if(EMSCRIPTEN)
    add_executable(markdown_parser_wasm
        src/ScannedLine.cpp
        src/SpineHandler.cpp
        src/InlineParser.cpp
        src/block_rules.cpp
        src/HtmlRenderer.cpp
        src/HtmlRenderDebug.cpp
        src/HtmlRendererFactory.cpp
        src/HandlerRegistry.cpp
        src/JsonRenderer.cpp
        src/entities.cpp
        src/string_utils.cpp
        src/unicode_fold.cpp
        src/handlers/MermaidHandler.cpp
        src/handlers/MathHandler.cpp
        src/wasm_bindings.cpp
    )
    target_include_directories(markdown_parser_wasm PRIVATE include)
    set_target_properties(markdown_parser_wasm PROPERTIES
        OUTPUT_NAME "markdown_parser"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/dist"
        SUFFIX ".js"
    )
    target_link_options(markdown_parser_wasm PRIVATE
        --bind
        -O2
    )
endif()
