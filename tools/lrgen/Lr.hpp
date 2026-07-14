#pragma once

#include "GrammarSpec.hpp"

#include <compare>
#include <map>
#include <set>
#include <string>
#include <vector>

// Canonical LR(1) construction: FIRST/NULLABLE, the item automaton
// (closure/goto/collection), and ACTION/GOTO table building with conflict
// detection. No LALR merge — states are distinguished by their full LR(1)
// item sets (cores + lookaheads).

namespace lrgen {

// An LR(0) core item: which production, and how far the dot has advanced.
struct Item {
  int prod;
  int dot;
  auto operator<=>(const Item &) const = default;
};

using Lookahead = std::set<int>;      // terminal symbol ids
using ItemSet = std::map<Item, Lookahead>; // core -> its lookahead terminals

struct State {
  ItemSet items;             // closed item set
  std::map<int, int> trans;  // symbol id -> target state id
};

struct Analysis {
  std::vector<bool> nullable;         // per symbol id
  std::vector<std::set<int>> first;   // per symbol id -> terminal ids
};

struct LRAction {
  enum Kind { Error, Shift, Reduce, Accept };
  Kind kind = Error;
  int arg = 0; // Shift: target state. Reduce: production id.
};

struct Conflict {
  int state;
  int terminal;      // symbol id
  std::string kind;  // "shift/reduce" | "reduce/reduce"
  std::string detail;
};

struct Tables {
  std::vector<std::vector<LRAction>> action; // [state][terminal column]
  std::vector<std::vector<int>> go;          // [state][nonterminal column], -1 = none
  std::vector<Conflict> conflicts;
};

Analysis analyze(const Grammar &g);
std::vector<State> build_states(const Grammar &g, const Analysis &an);
Tables build_tables(const Grammar &g, const std::vector<State> &states);

} // namespace lrgen
