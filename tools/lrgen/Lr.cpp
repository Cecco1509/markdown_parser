#include "Lr.hpp"

#include <deque>
#include <map>
#include <utility>

namespace lrgen {
namespace {

// FIRST of the symbol sequence rhs[from..], plus whether it is nullable.
std::pair<std::set<int>, bool> first_of_seq(const Analysis &an,
                                            const std::vector<int> &rhs, int from) {
  std::set<int> res;
  for (int i = from; i < (int)rhs.size(); ++i) {
    int X = rhs[i];
    res.insert(an.first[X].begin(), an.first[X].end());
    if (!an.nullable[X]) return {res, false};
  }
  return {res, true}; // fell off the end => nullable
}

// Complete an item set in place (LR(1) closure).
void closure(const Grammar &g, const Analysis &an, ItemSet &I) {
  std::deque<Item> work;
  for (auto &[it, la] : I) work.push_back(it);

  while (!work.empty()) {
    Item it = work.front();
    work.pop_front();
    Lookahead la = I[it]; // snapshot of current lookahead
    const Production &P = g.productions[it.prod];
    if (it.dot >= (int)P.rhs.size()) continue;
    int B = P.rhs[it.dot];
    if (g.is_terminal(B)) continue;

    auto [fb, nul] = first_of_seq(an, P.rhs, it.dot + 1);
    Lookahead look = fb;
    if (nul) look.insert(la.begin(), la.end());

    for (int p = 0; p < (int)g.productions.size(); ++p) {
      if (g.productions[p].lhs != B) continue;
      Item child{p, 0};
      bool existed = I.count(child) != 0;
      Lookahead &dst = I[child];
      size_t before = dst.size();
      dst.insert(look.begin(), look.end());
      if (!existed || dst.size() != before) work.push_back(child);
    }
  }
}

ItemSet goto_set(const Grammar &g, const Analysis &an, const ItemSet &I, int X) {
  ItemSet K;
  for (const auto &[it, la] : I) {
    const Production &P = g.productions[it.prod];
    if (it.dot < (int)P.rhs.size() && P.rhs[it.dot] == X) {
      Lookahead &dst = K[Item{it.prod, it.dot + 1}];
      dst.insert(la.begin(), la.end());
    }
  }
  closure(g, an, K);
  return K;
}

} // namespace

Analysis analyze(const Grammar &g) {
  int n = (int)g.symbols.size();
  Analysis an;
  an.nullable.assign(n, false);
  an.first.assign(n, {});
  for (int s = 0; s < n; ++s)
    if (g.is_terminal(s)) an.first[s].insert(s);

  bool changed = true;
  while (changed) {
    changed = false;
    for (const Production &P : g.productions) {
      int A = P.lhs;
      bool all_null = true;
      for (int X : P.rhs) {
        size_t before = an.first[A].size();
        an.first[A].insert(an.first[X].begin(), an.first[X].end());
        if (an.first[A].size() != before) changed = true;
        if (!an.nullable[X]) { all_null = false; break; }
      }
      if (all_null && !an.nullable[A]) { an.nullable[A] = true; changed = true; }
    }
  }
  return an;
}

std::vector<State> build_states(const Grammar &g, const Analysis &an) {
  ItemSet kernel;
  kernel[Item{0, 0}] = {g.end_sym}; // [$accept -> . start, $end]

  std::vector<State> states;
  std::map<ItemSet, int> index;

  State s0;
  s0.items = kernel;
  closure(g, an, s0.items);
  states.push_back(std::move(s0));
  index[states[0].items] = 0;

  std::deque<int> work{0};
  while (!work.empty()) {
    int s = work.front();
    work.pop_front();

    // gather the symbols appearing immediately after a dot
    std::set<int> syms;
    for (const auto &[it, la] : states[s].items) {
      const Production &P = g.productions[it.prod];
      if (it.dot < (int)P.rhs.size()) syms.insert(P.rhs[it.dot]);
    }

    for (int X : syms) {
      ItemSet J = goto_set(g, an, states[s].items, X);
      if (J.empty()) continue;
      int t;
      if (auto f = index.find(J); f != index.end()) {
        t = f->second;
      } else {
        t = (int)states.size();
        State st;
        st.items = J;
        states.push_back(std::move(st));
        index[states.back().items] = t;
        work.push_back(t);
      }
      states[s].trans[X] = t;
    }
  }
  return states;
}

Tables build_tables(const Grammar &g, const std::vector<State> &states) {
  Tables T;
  int nS = (int)states.size();
  int nT = g.num_terminals();
  int nN = g.num_nonterminals();
  T.action.assign(nS, std::vector<LRAction>(nT));
  T.go.assign(nS, std::vector<int>(nN, -1));

  auto set_action = [&](int s, int term_sym, LRAction a) {
    int col = g.term_col[term_sym];
    LRAction &cell = T.action[s][col];
    if (cell.kind == LRAction::Error) { cell = a; return; }
    if (cell.kind == a.kind && cell.arg == a.arg) return; // identical, fine
    std::string kind =
        (cell.kind == LRAction::Shift || a.kind == LRAction::Shift)
            ? "shift/reduce"
            : "reduce/reduce";
    T.conflicts.push_back({s, term_sym, kind,
                           "keeping existing action on '" +
                               g.symbols[term_sym].name + "'"});
  };

  for (int s = 0; s < nS; ++s) {
    // shifts / gotos from transitions
    for (const auto &[X, t] : states[s].trans) {
      if (g.is_terminal(X))
        set_action(s, X, {LRAction::Shift, t});
      else
        T.go[s][g.nt_col[X]] = t;
    }
    // reduces / accept from complete items
    for (const auto &[it, la] : states[s].items) {
      const Production &P = g.productions[it.prod];
      if (it.dot != (int)P.rhs.size()) continue;
      if (it.prod == 0) {
        for (int t : la) set_action(s, t, {LRAction::Accept, 0});
      } else {
        for (int t : la) set_action(s, t, {LRAction::Reduce, it.prod});
      }
    }
  }
  return T;
}

} // namespace lrgen
