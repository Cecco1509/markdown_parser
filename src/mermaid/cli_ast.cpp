#include "mermaid/AstDump.hpp"
#include "mermaid/FlowParse.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// mermaid_ast [file.mmd]
// Reads a mermaid flowchart from the given file (or stdin) and prints the AST.

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
  if (r.document) std::cout << mermaid::dump_ast(*r.document);
  return r.ok ? 0 : 1;
}
