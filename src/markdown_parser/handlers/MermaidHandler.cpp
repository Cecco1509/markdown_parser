#include "markdown_parser/handlers/HandlerRegistry.hpp"

#include "mermaid/FlowParse.hpp"
#include "mermaid/Layout.hpp"
#include "mermaid/Lower.hpp"
#include "mermaid/SvgRenderer.hpp"
#include "mermaid/TextMeasure.hpp"

#include <memory>
#include <string>

// Fenced ```mermaid``` blocks are rendered to inline SVG through the mermaid
// pipeline (parse -> lower -> layout -> SVG). The only per-build difference is
// label measurement: the web build uses the browser's font engine, the native
// build a heuristic. See docs/mermaid/rendering.md.

static std::string passthrough(const std::string &src) {
  return "<div class=\"mermaid\">\n" + src + "</div>\n";
}

// Choose the label measurer for the current build.
static std::unique_ptr<mermaid::TextMeasurer> make_measurer() {
#ifdef __EMSCRIPTEN__
  return mermaid::make_browser_measurer(); // real browser font metrics
#else
  return std::make_unique<mermaid::ApproxMeasurer>(); // offline heuristic
#endif
}

static std::string render_mermaid(const std::string &src) {
  mermaid::FlowParseResult parsed = mermaid::parse_flowchart(src);
  if (!parsed.ok || !parsed.document)
    return passthrough(src); // keep the block rather than lose it on error

  mermaid::FlowDb db = mermaid::lower(*parsed.document);
  std::unique_ptr<mermaid::TextMeasurer> measurer = make_measurer();
  mermaid::Layout laid = mermaid::layout(db, *measurer);
  return "<div class=\"mermaid\">\n" + mermaid::render_svg(laid) + "\n</div>\n";
}

static bool registered =
    markdown_parser::HandlerRegistry::add("mermaid", {"mermaid"}, render_mermaid);
