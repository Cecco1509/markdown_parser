#include "mermaid/FlowParse.hpp"
#include "mermaid/Layout.hpp"
#include "mermaid/Lower.hpp"
#include "mermaid/SvgRenderer.hpp"
#include "mermaid/TextMeasure.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// mermaid_svg [file.mmd]
// Reads a mermaid flowchart (file or stdin) and prints the rendered SVG.
// Pipeline: lex -> parse -> lower -> layout(ApproxMeasurer) -> render_svg.

int main(int argc, char **argv) {
  std::string src;
  if (argc >= 2) {
    std::ifstream f(argv[1]);
    if (!f) {
      std::cerr << "cannot open " << argv[1] << "\n";
      return 1;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    src = ss.str();
  } else {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    src = ss.str();
  }

  mermaid::FlowParseResult r = mermaid::parse_flowchart(src);
  for (const std::string &e : r.errors) std::cerr << "error: " << e << "\n";
  if (!r.ok || !r.document) return 1;

  mermaid::FlowDb db = mermaid::lower(*r.document);
  mermaid::ApproxMeasurer measurer;
  mermaid::Layout laid = mermaid::layout(db, measurer);
  std::cout << mermaid::render_svg(laid) << "\n";
  return 0;
}
