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
    src/markdown_parser/parser/ScannedLine.cpp
    src/markdown_parser/parser/SpineHandler.cpp
    src/markdown_parser/parser/InlineParser.cpp
    src/markdown_parser/parser/block_rules.cpp
    src/markdown_parser/renderer/HtmlRenderer.cpp
    src/markdown_parser/renderer/HtmlRenderDebug.cpp
    src/markdown_parser/renderer/HtmlRendererFactory.cpp
    src/markdown_parser/handlers/HandlerRegistry.cpp
    src/markdown_parser/renderer/JsonRenderer.cpp
    src/markdown_parser/utils/entities.cpp
    src/markdown_parser/utils/string_utils.cpp
    src/markdown_parser/utils/unicode_fold.cpp
)
target_include_directories(md_parser PUBLIC include)

if(NOT EMSCRIPTEN)
    add_executable(md_parser_bin
        src/main.cpp
        src/markdown_parser/handlers/MermaidHandler.cpp
        src/markdown_parser/handlers/MathHandler.cpp
    )
    target_link_libraries(md_parser_bin PRIVATE md_parser)

    enable_testing()
    add_subdirectory(tests)
endif()

# WebAssembly build target (only when using Emscripten)
if(EMSCRIPTEN)
    add_executable(markdown_parser_wasm
        src/markdown_parser/parser/ScannedLine.cpp
        src/markdown_parser/parser/SpineHandler.cpp
        src/markdown_parser/parser/InlineParser.cpp
        src/markdown_parser/parser/block_rules.cpp
        src/markdown_parser/renderer/HtmlRenderer.cpp
        src/markdown_parser/renderer/HtmlRenderDebug.cpp
        src/markdown_parser/renderer/HtmlRendererFactory.cpp
        src/markdown_parser/handlers/HandlerRegistry.cpp
        src/markdown_parser/renderer/JsonRenderer.cpp
        src/markdown_parser/utils/entities.cpp
        src/markdown_parser/utils/string_utils.cpp
        src/markdown_parser/utils/unicode_fold.cpp
        src/markdown_parser/handlers/MermaidHandler.cpp
        src/markdown_parser/handlers/MathHandler.cpp
        src/wasm_bindings.cpp
    )
    # Handler sources are linked directly into the executable (not via the
    # static library) so their static-initializer self-registration is not
    # dropped by the linker.
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
