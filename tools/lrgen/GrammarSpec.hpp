#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// Data model for a parsed grammar, shared by every phase of lrgen.
// Symbols (terminals + nonterminals) live in one vector; a symbol is referred
// to everywhere by its integer id (index into `symbols`).

namespace lrgen {

// Reserved sentinel symbol names the generator adds during augmentation.
// `kEndSymbol` is also matched by name in the emitter, so both sites must agree.
inline constexpr const char *kEndSymbol = "$end";       // end-of-input terminal
inline constexpr const char *kAcceptSymbol = "$accept"; // augmented start rule

enum class SymKind { Terminal, Nonterminal };

struct Symbol {
  std::string name;
  SymKind kind = SymKind::Nonterminal;
  std::optional<std::string> type; // C++ semantic type; nullopt = value-less
};

struct Production {
  int lhs = -1;             // symbol id (a nonterminal)
  std::vector<int> rhs;     // symbol ids
  std::string action;       // opaque C++ expression with $k holes ("" for aug)
  int line = 0;             // source line, for diagnostics
};

struct Grammar {
  std::vector<Symbol> symbols;
  std::vector<Production> productions; // [0] is the augmented S' -> start

  std::vector<std::string> includes;   // %include paths, emitted verbatim
  std::string nspace = "parser";       // %namespace

  int start_sym = -1; // user's start nonterminal
  int aug_sym = -1;   // S' ($accept)
  int end_sym = -1;   // $end terminal

  std::map<std::string, int> ids; // name -> symbol id

  // Column layouts, filled by finalize():
  std::vector<int> terminals;    // symbol ids, defines ACTION column order
  std::vector<int> nonterminals; // symbol ids, defines GOTO column order
  std::vector<int> term_col;     // per symbol id -> terminal column, or -1
  std::vector<int> nt_col;       // per symbol id -> nonterminal column, or -1

  bool is_terminal(int s) const { return symbols[s].kind == SymKind::Terminal; }
  int num_terminals() const { return static_cast<int>(terminals.size()); }
  int num_nonterminals() const { return static_cast<int>(nonterminals.size()); }
};

} // namespace lrgen
