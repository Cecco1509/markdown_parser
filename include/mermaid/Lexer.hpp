#pragma once

#include "mermaid/Token.hpp"

#include <string>
#include <string_view>
#include <vector>

// ── Lexer ─────────────────────────────────────────────────────────────────
// Turns mermaid flowchart source into a flat token stream (terminated by
// PunctTok{End}) that the LR parser consumes. All lexical ambiguity is resolved
// here so the downstream grammar is conflict-free:
//
//   * edge operators (-->, -.->, ==>, ---, --o, --x, <-->, ~~~, and their
//     variable-length forms) are matched greedily into a single LinkTok;
//   * inline edge labels (A -- text --> B) are captured onto the LinkTok;
//   * bracket shapes are matched with longest-open / scan-to-matching-close,
//     with quoted content skipped so `]` inside a label does not close it;
//   * reserved keywords and direction codes are classified unconditionally.
//
// Front-matter (--- ... ---) and the %%{init:}%% directive are lifted out of the
// token stream and returned raw for a later config pass; `%%` comment lines are
// dropped.
// ---------------------------------------------------------------------------

namespace mermaid {

struct LexResult {
  std::vector<Token> tokens;    // always terminated by PunctTok{End}
  std::string front_matter;     // raw YAML between leading --- --- (no fences)
  std::string init_directive;   // raw text of a %%{init: ...}%% line
  std::vector<std::string> errors;
};

LexResult lex(std::string_view src);

} // namespace mermaid
