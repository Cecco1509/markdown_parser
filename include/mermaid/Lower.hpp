#pragma once

#include "mermaid/FlowDb.hpp"
#include "mermaid/ast/FlowAst.hpp"

// Lowers the flowchart parse tree into the semantic FlowDb: expands & groups
// into edges, merges repeated node definitions (last-label-wins), resolves
// classes/styles, and collects subgraph membership. See docs/mermaid/deferred.md
// for what is intentionally not handled yet (click, linkStyle, title).

namespace mermaid {

FlowDb lower(const ast::Document &doc);

} // namespace mermaid
