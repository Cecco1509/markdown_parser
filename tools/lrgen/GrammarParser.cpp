#include "GrammarParser.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lrgen {
namespace {

struct RawProd {
  std::string lhs;
  std::vector<std::string> rhs;
  std::string action;
  int line = 0;
};

// A tiny recursive-descent front-end over the grammar text.
class Cursor {
public:
  explicit Cursor(const std::string &src) : s_(src) {}

  Grammar parse() {
    parse_declarations();
    parse_rules();
    return finalize();
  }

private:
  // ── low-level scanning ────────────────────────────────────────────────
  char cur() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
  char at(size_t i) const { return i < s_.size() ? s_[i] : '\0'; }

  void adv() {
    if (pos_ < s_.size()) {
      if (s_[pos_] == '\n') ++line_;
      ++pos_;
    }
  }

  [[noreturn]] void fail(const std::string &msg) const {
    throw std::runtime_error("grammar:" + std::to_string(line_) + ": " + msg);
  }

  void skip_ws() {
    for (;;) {
      char c = cur();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { adv(); continue; }
      if (c == '#') { // comment to end of line
        while (cur() != '\0' && cur() != '\n') adv();
        continue;
      }
      break;
    }
  }

  bool looking_at(const char *kw) {
    size_t i = pos_;
    for (const char *p = kw; *p; ++p, ++i)
      if (at(i) != *p) return false;
    return true;
  }

  bool eat(const char *kw) {
    if (!looking_at(kw)) return false;
    for (const char *p = kw; *p; ++p) adv();
    return true;
  }

  static bool id_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
  static bool id_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

  std::string read_ident() {
    if (!id_start(cur())) fail("expected identifier");
    std::string r;
    while (id_char(cur())) { r += cur(); adv(); }
    return r;
  }

  // qualified name for %namespace (allows ::)
  std::string read_qualified() {
    std::string r;
    while (id_char(cur()) || cur() == ':') { r += cur(); adv(); }
    if (r.empty()) fail("expected name");
    return r;
  }

  std::string read_angle_type() {
    if (cur() != '<') fail("expected '<' for a type");
    adv();
    std::string r;
    int depth = 1;
    while (depth > 0) {
      char c = cur();
      if (c == '\0') fail("unterminated <type>");
      if (c == '<') ++depth;
      else if (c == '>') { if (--depth == 0) { adv(); break; } }
      r += c;
      adv();
    }
    return trim(r);
  }

  std::string read_action() {
    if (cur() != '{') fail("expected '{'");
    adv();
    std::string r;
    int depth = 1;
    while (depth > 0) {
      char c = cur();
      if (c == '\0') fail("unterminated { action }");
      if (c == '{') ++depth;
      else if (c == '}') { if (--depth == 0) { adv(); break; } }
      r += c;
      adv();
    }
    return trim(r);
  }

  std::string read_string() {
    if (cur() != '"') fail("expected '\"'");
    adv();
    std::string r;
    while (cur() != '"' && cur() != '\0') { r += cur(); adv(); }
    if (cur() != '"') fail("unterminated string");
    adv();
    return r;
  }

  static std::string trim(const std::string &x) {
    size_t b = 0, e = x.size();
    while (b < e && std::isspace((unsigned char)x[b])) ++b;
    while (e > b && std::isspace((unsigned char)x[e - 1])) --e;
    return x.substr(b, e - b);
  }

  // ── declarations ──────────────────────────────────────────────────────
  void parse_declarations() {
    for (;;) {
      skip_ws();
      if (cur() == '\0') fail("missing %% before rules");
      if (looking_at("%%")) { eat("%%"); return; }
      if (cur() != '%') fail("expected a %directive or %%");
      adv(); // consume '%'
      std::string dir = read_ident();
      if (dir == "start") {
        skip_ws();
        start_name_ = read_ident();
      } else if (dir == "token") {
        parse_symbol_decl(/*terminal=*/true);
      } else if (dir == "type") {
        parse_symbol_decl(/*terminal=*/false);
      } else if (dir == "include") {
        skip_ws();
        includes_.push_back(read_string());
      } else if (dir == "namespace") {
        skip_ws();
        nspace_ = read_qualified();
      } else {
        fail("unknown directive %" + dir);
      }
    }
  }

  // %token [<Type>] NAME NAME ...   /   %type <Type> NAME NAME ...
  void parse_symbol_decl(bool terminal) {
    skip_ws();
    std::optional<std::string> type;
    if (cur() == '<') type = read_angle_type();
    for (;;) {
      skip_ws();
      if (!id_start(cur())) break; // next directive or %% stops the list
      std::string name = read_ident();
      if (terminal) {
        term_type_[name] = type;
        if (!seen_term_.count(name)) { term_order_.push_back(name); seen_term_.insert(name); }
      } else {
        if (type) nt_type_[name] = *type;
      }
    }
  }

  // ── rules ─────────────────────────────────────────────────────────────
  void parse_rules() {
    for (;;) {
      skip_ws();
      if (cur() == '\0') break;
      int line = line_;
      std::string lhs = read_ident();
      if (!seen_nt_.count(lhs)) { nt_order_.push_back(lhs); seen_nt_.insert(lhs); }
      skip_ws();
      if (cur() != ':') fail("expected ':' after '" + lhs + "'");
      adv();
      for (;;) {
        RawProd p;
        p.lhs = lhs;
        p.line = line;
        parse_alt(p);
        raw_.push_back(std::move(p));
        skip_ws();
        if (cur() == '|') { adv(); continue; }
        if (cur() == ';') { adv(); break; }
        fail("expected '|' or ';'");
      }
    }
  }

  void parse_alt(RawProd &p) {
    for (;;) {
      skip_ws();
      char c = cur();
      if (c == '{') { p.action = read_action(); return; }
      if (c == '|' || c == ';') return;
      if (c == '%') {
        if (eat("%empty")) continue; // empty RHS marker
        fail("unexpected '%' in rule body");
      }
      if (c == '\0') fail("unexpected end of file in rule body");
      p.rhs.push_back(read_ident());
    }
  }

  // ── finalize: ids, augmentation, validation ───────────────────────────
  int add_symbol(const std::string &name, SymKind kind,
                 std::optional<std::string> type) {
    int id = static_cast<int>(g_.symbols.size());
    g_.symbols.push_back({name, kind, std::move(type)});
    g_.ids[name] = id;
    return id;
  }

  Grammar finalize() {
    if (start_name_.empty()) fail("missing %start");

    g_.includes = includes_;
    g_.nspace = nspace_;

    // terminals (declaration order), then $end
    for (const std::string &t : term_order_)
      add_symbol(t, SymKind::Terminal, term_type_[t]);
    g_.end_sym = add_symbol("$end", SymKind::Terminal, std::nullopt);

    // nonterminals (first-appearance order), then $accept
    for (const std::string &nt : nt_order_) {
      std::optional<std::string> ty;
      if (auto it = nt_type_.find(nt); it != nt_type_.end()) ty = it->second;
      add_symbol(nt, SymKind::Nonterminal, ty);
    }
    g_.aug_sym = add_symbol("$accept", SymKind::Nonterminal, std::nullopt);

    if (!g_.ids.count(start_name_))
      fail("%start '" + start_name_ + "' is not a defined nonterminal");
    g_.start_sym = g_.ids[start_name_];

    // production 0: $accept -> start
    g_.productions.push_back({g_.aug_sym, {g_.start_sym}, "", 0});

    // user productions, resolving RHS names
    for (const RawProd &rp : raw_) {
      Production p;
      p.lhs = g_.ids.at(rp.lhs);
      p.action = rp.action;
      p.line = rp.line;
      for (const std::string &name : rp.rhs) {
        auto it = g_.ids.find(name);
        if (it == g_.ids.end())
          throw std::runtime_error("grammar:" + std::to_string(rp.line) +
                                   ": undeclared symbol '" + name + "'");
        p.rhs.push_back(it->second);
      }
      g_.productions.push_back(std::move(p));
    }

    build_columns();
    validate_actions();
    return std::move(g_);
  }

  void build_columns() {
    g_.term_col.assign(g_.symbols.size(), -1);
    g_.nt_col.assign(g_.symbols.size(), -1);
    for (int s = 0; s < (int)g_.symbols.size(); ++s) {
      if (g_.symbols[s].kind == SymKind::Terminal) {
        g_.term_col[s] = (int)g_.terminals.size();
        g_.terminals.push_back(s);
      } else {
        g_.nt_col[s] = (int)g_.nonterminals.size();
        g_.nonterminals.push_back(s);
      }
    }
  }

  // Check every $k in an action: k in range, and symbol k is value-carrying.
  void validate_actions() {
    for (const Production &p : g_.productions) {
      const std::string &a = p.action;
      for (size_t i = 0; i + 1 < a.size(); ++i) {
        if (a[i] != '$' || !std::isdigit((unsigned char)a[i + 1])) continue;
        size_t j = i + 1;
        int k = 0;
        while (j < a.size() && std::isdigit((unsigned char)a[j])) {
          k = k * 10 + (a[j] - '0');
          ++j;
        }
        if (k < 1 || k > (int)p.rhs.size())
          throw std::runtime_error("grammar:" + std::to_string(p.line) +
                                   ": $" + std::to_string(k) + " out of range");
        int sym = p.rhs[k - 1];
        if (!g_.symbols[sym].type)
          throw std::runtime_error(
              "grammar:" + std::to_string(p.line) + ": $" + std::to_string(k) +
              " refers to value-less symbol '" + g_.symbols[sym].name + "'");
      }
    }
  }

  // state
  const std::string &s_;
  size_t pos_ = 0;
  int line_ = 1;

  std::string start_name_;
  std::string nspace_ = "parser";
  std::vector<std::string> includes_;

  std::vector<std::string> term_order_;
  std::set<std::string> seen_term_;
  std::map<std::string, std::optional<std::string>> term_type_;

  std::vector<std::string> nt_order_;
  std::set<std::string> seen_nt_;
  std::map<std::string, std::string> nt_type_;

  std::vector<RawProd> raw_;
  Grammar g_;
};

} // namespace

Grammar parse_grammar(const std::string &text) { return Cursor(text).parse(); }

} // namespace lrgen
