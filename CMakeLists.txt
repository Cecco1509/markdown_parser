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

# Standalone mermaid flowchart module (parse -> layout -> SVG). Independent of
# markdown; the markdown fence handler is the only coupling point.
add_library(mermaid
    src/mermaid/Lexer.cpp
    src/mermaid/Lower.cpp
    src/mermaid/TextMeasure.cpp
    src/mermaid/Layout.cpp
    src/mermaid/SvgRenderer.cpp
    src/mermaid/AstDump.cpp
)
target_include_directories(mermaid PUBLIC include)

# ── mermaid parser codegen (native AND wasm) ────────────────────────────────
# lrgen is a build-time HOST tool: it runs on the build machine to emit the
# flowchart parser; only its generated OUTPUT is compiled into the target.
# Native builds build lrgen normally. Under Emscripten the whole toolchain is
# emcc, so lrgen is built with the host toolchain via ExternalProject; its
# output is then compiled to wasm like any other source. Nothing generated is
# committed.
set(MERMAID_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set(MERMAID_GRAMMAR ${CMAKE_SOURCE_DIR}/src/mermaid/flowchart.grammar)
file(MAKE_DIRECTORY ${MERMAID_GEN_DIR})

if(EMSCRIPTEN)
    include(ExternalProject)
    ExternalProject_Add(lrgen_host
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools/lrgen
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS <BINARY_DIR>/lrgen
    )
    ExternalProject_Get_Property(lrgen_host BINARY_DIR)
    set(LRGEN_EXE ${BINARY_DIR}/lrgen)
    set(LRGEN_DEP lrgen_host)
else()
    add_subdirectory(tools/lrgen)
    set(LRGEN_EXE $<TARGET_FILE:lrgen>)
    set(LRGEN_DEP lrgen)
endif()

add_custom_command(
    OUTPUT ${MERMAID_GEN_DIR}/flowchart_parser.hpp
           ${MERMAID_GEN_DIR}/flowchart_parser.cpp
    COMMAND ${LRGEN_EXE} ${MERMAID_GRAMMAR} ${MERMAID_GEN_DIR}/flowchart_parser
    DEPENDS ${LRGEN_DEP} ${MERMAID_GRAMMAR}
    COMMENT "lrgen: generating flowchart parser"
    VERBATIM
)
target_sources(mermaid PRIVATE
    ${MERMAID_GEN_DIR}/flowchart_parser.cpp
    src/mermaid/FlowParse.cpp
)
target_include_directories(mermaid PUBLIC ${MERMAID_GEN_DIR})

if(NOT EMSCRIPTEN)
    # CLI: parse a .mmd (file arg or stdin) and print the AST.
    add_executable(mermaid_ast src/mermaid/cli_ast.cpp)
    target_link_libraries(mermaid_ast PRIVATE mermaid)

    # CLI: parse + layout + render a .mmd to SVG.
    add_executable(mermaid_svg src/mermaid/cli_svg.cpp)
    target_link_libraries(mermaid_svg PRIVATE mermaid)

    add_executable(md_parser_bin
        src/main.cpp
        src/markdown_parser/handlers/MermaidHandler.cpp
        src/markdown_parser/handlers/MathHandler.cpp
    )
    # MermaidHandler renders fenced blocks via the mermaid pipeline.
    target_link_libraries(md_parser_bin PRIVATE md_parser mermaid)

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
        # Browser-backed label measurer (WASM only); plugs in behind TextMeasurer.
        src/mermaid/BrowserMeasure.cpp
        src/wasm_bindings.cpp
    )
    # Handler sources are linked directly into the executable (not via the
    # static library) so their static-initializer self-registration is not
    # dropped by the linker.
    target_include_directories(markdown_parser_wasm PRIVATE include)
    # The mermaid pipeline (parser/lower/layout/render) is compiled to wasm here.
    target_link_libraries(markdown_parser_wasm PRIVATE mermaid)
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
