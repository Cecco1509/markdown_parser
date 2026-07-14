#include "Emitter.hpp"
#include "GrammarParser.hpp"
#include "Lr.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace lrgen;

namespace {

std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string base_name(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: lrgen <grammar> <out_base>\n"
                 "  emits <out_base>.hpp and <out_base>.cpp\n";
    return 64;
  }
  const std::string grammar_path = argv[1];
  const std::string out_base = argv[2];

  try {
    Grammar g = parse_grammar(read_file(grammar_path));
    Analysis an = analyze(g);
    std::vector<State> states = build_states(g, an);
    Tables tables = build_tables(g, states);

    std::cerr << "lrgen: " << g.symbols.size() << " symbols ("
              << g.num_terminals() << " terminals, " << g.num_nonterminals()
              << " nonterminals), " << g.productions.size() << " productions, "
              << states.size() << " LR(1) states\n";

    if (!tables.conflicts.empty()) {
      std::cerr << "lrgen: " << tables.conflicts.size()
                << " CONFLICT(S) — grammar is not LR(1):\n";
      for (const Conflict &c : tables.conflicts)
        std::cerr << "  state " << c.state << " on '"
                  << g.symbols[c.terminal].name << "': " << c.kind << " ("
                  << c.detail << ")\n";
      return 65;
    }

    emit(g, states, tables, out_base, base_name(out_base) + ".hpp");
    std::cerr << "lrgen: wrote " << out_base << ".hpp and " << out_base
              << ".cpp\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "lrgen: error: " << e.what() << "\n";
    return 1;
  }
}
