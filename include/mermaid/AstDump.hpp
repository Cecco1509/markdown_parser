#pragma once

#include "mermaid/ast/FlowAst.hpp"

#include <string>

// Renders a parsed flowchart AST as an indented, human-readable tree. For
// debugging / visualizing the parser output.

namespace mermaid {

std::string dump_ast(const ast::Document &doc);

} // namespace mermaid
