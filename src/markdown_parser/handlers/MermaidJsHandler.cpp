#include "markdown_parser/handlers/HandlerRegistry.hpp"

#include <string>

// Fenced ```mermaid``` blocks handled for the client-side mermaid.js library.
//
// Unlike the native "mermaid" handler (which renders to inline SVG server-side),
// this handler emits the raw diagram source inside a <div class="mermaid"> so
// that the mermaid.js runtime in the browser can render it. This is the same
// markup the native handler falls back to on a parse error, so mermaid.js can
// also act as a fallback renderer for those blocks. See docs/mermaid/rendering.md.

static bool registered = markdown_parser::HandlerRegistry::add(
    "mermaidjs", {"mermaid"}, [](const std::string &src) {
      return "<div class=\"mermaid\">\n" + src + "</div>\n";
    });
