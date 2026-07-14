#pragma once

#include "mermaid/ast/FlowAst.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Public entry point for the mermaid flowchart front-end: lex -> adapt -> parse.
// Returns the Flow AST. (A later lower(Document) -> FlowDb pass will build the
// final diagram model.) This header intentionally depends only on FlowAst.hpp,
// not on the build-tree generated parser header.

namespace mermaid {

struct FlowParseResult {
  std::optional<ast::Document> document;
  std::vector<std::string> errors;
  bool ok = false;
};

FlowParseResult parse_flowchart(std::string_view src);

} // namespace mermaid
