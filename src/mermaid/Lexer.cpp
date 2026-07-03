#include "mermaid/Lexer.hpp"

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mermaid {
namespace {

// ── small string helpers ──────────────────────────────────────────────────
bool is_id_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool is_id_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

// Strip a single layer of matching "..." or `...` wrapping, if present.
std::string strip_quotes(std::string s) {
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '`' && s.back() == '`'))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

std::string clean_label(std::string_view raw) {
  return strip_quotes(trim(raw));
}

std::optional<Punct> keyword_of(const std::string &w) {
  if (w == "graph" || w == "flowchart") return Punct::KwGraph;
  if (w == "subgraph") return Punct::KwSubgraph;
  if (w == "end") return Punct::KwEnd;
  if (w == "direction") return Punct::KwDirection;
  if (w == "style") return Punct::KwStyle;
  if (w == "classDef") return Punct::KwClassDef;
  if (w == "class") return Punct::KwClass;
  if (w == "linkStyle") return Punct::KwLinkStyle;
  if (w == "click") return Punct::KwClick;
  if (w == "default") return Punct::KwDefault;
  return std::nullopt;
}

std::optional<Direction> dir_of(const std::string &w) {
  if (w == "TB" || w == "TD") return Direction::TB;
  if (w == "BT") return Direction::BT;
  if (w == "LR") return Direction::LR;
  if (w == "RL") return Direction::RL;
  return std::nullopt;
}

ArrowHead head_of(char c) {
  switch (c) {
  case '>': return ArrowHead::Arrow;
  case 'o': return ArrowHead::Circle;
  case 'x': return ArrowHead::Cross;
  default: return ArrowHead::None;
  }
}
bool is_head_char(char c) { return c == '>' || c == 'o' || c == 'x'; }

// Result of matching a self-contained ("compact", no interior spaces) edge
// operator such as -->, -.->, ==>, ---, --o, <not the leading '<'>.
struct OpInfo {
  size_t len;      // characters consumed from the match position
  Stroke stroke;
  ArrowHead head;  // None for an open link
  int length;      // rank span derived from extra dashes/dots/equals
};

// ── the lexer proper ──────────────────────────────────────────────────────
class Lexer {
public:
  explicit Lexer(std::string_view src) : src_(src) {}

  LexResult run() {
    handle_front_matter();
    bool line_start = true;

    while (cur() != '\0') {
      if (line_start) {
        while (cur() == ' ' || cur() == '\t' || cur() == '\r') adv();
        if (cur() == '\0') break;
        if (cur() == '\n') { emit(PunctTok{Punct::Newline}); adv(); continue; }
        if (starts_with(pos_, "%%{")) { handle_init_directive(); continue; }
        if (starts_with(pos_, "%%")) { skip_to_eol(); continue; }
        line_start = false;
      }

      const char c = cur();
      switch (c) {
      case '\n': emit(PunctTok{Punct::Newline}); adv(); line_start = true; break;
      case '\r':
      case ' ':
      case '\t': adv(); break;
      case ';': emit(PunctTok{Punct::Newline}); adv(); break;
      case '&': emit(PunctTok{Punct::Ampersand}); adv(); break;
      case ',': emit(PunctTok{Punct::Comma}); adv(); break;
      case '|': scan_pipe_label(); break;
      case '"': scan_string(); break;
      case '<':
      case '-':
      case '=':
      case '~': scan_edge(); break;
      case ':':
        if (at(pos_ + 1) == ':' && at(pos_ + 2) == ':') {
          emit(PunctTok{Punct::TripleColon}); adv(3);
        } else {
          error("unexpected ':'"); adv();
        }
        break;
      case '%':
        // A mid-line %% is NOT a comment (mermaid: only line-leading %% opens
        // one). It is a parse error / ignored depending on version; we ignore
        // the remainder of the line rather than abort.
        if (at(pos_ + 1) == '%') { skip_to_eol(); }
        else { error("unexpected '%'"); adv(); }
        break;
      default:
        if (is_digit(c)) scan_number();
        else if (is_id_start(c)) scan_word();
        else { error(std::string("unexpected character '") + c + "'"); adv(); }
      }
    }

    emit(PunctTok{Punct::End});
    return std::move(result_);
  }

private:
  // ── cursor ───────────────────────────────────────────────────────────
  char cur() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
  char at(size_t i) const { return i < src_.size() ? src_[i] : '\0'; }

  void adv(size_t k = 1) {
    while (k-- && pos_ < src_.size()) {
      if (src_[pos_] == '\n') { ++line_; col_ = 1; }
      else { ++col_; }
      ++pos_;
    }
  }

  bool starts_with(size_t p, std::string_view s) const {
    return src_.substr(p, s.size()) == s;
  }
  void skip_to_eol() {
    while (cur() != '\0' && cur() != '\n') adv();
  }

  void emit(TokenValue v) { emit(std::move(v), line_, col_); }
  void emit(TokenValue v, int line, int col) {
    result_.tokens.push_back(Token{std::move(v), line, col});
  }
  void error(std::string msg) {
    result_.errors.push_back("line " + std::to_string(line_) + ": " +
                             std::move(msg));
  }

  // ── front-matter / init directive ────────────────────────────────────
  void handle_front_matter() {
    size_t p = pos_;
    while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t')) ++p;
    if (!starts_with(p, "---")) return;
    size_t q = p + 3;
    while (q < src_.size() && (src_[q] == ' ' || src_[q] == '\t' || src_[q] == '\r'))
      ++q;
    if (!(q >= src_.size() || src_[q] == '\n')) return; // not a lone --- line

    size_t i = (q < src_.size()) ? q + 1 : q; // first content line
    std::string content;
    while (i < src_.size()) {
      size_t le = i;
      while (le < src_.size() && src_[le] != '\n') ++le;
      std::string line = src_.substr(i, le - i);
      if (trim(line) == "---") { i = (le < src_.size()) ? le + 1 : le; break; }
      content += line;
      content += '\n';
      if (le >= src_.size()) { i = le; break; }
      i = le + 1;
    }
    result_.front_matter = content;
    adv(i - pos_);
  }

  void handle_init_directive() {
    size_t s = pos_;
    skip_to_eol();
    result_.init_directive = trim(src_.substr(s, pos_ - s));
  }

  // ── identifiers, keywords, numbers ───────────────────────────────────
  void scan_word() {
    const int l = line_, c = col_;
    size_t s = pos_;
    while (is_id_char(cur())) adv();
    std::string w = src_.substr(s, pos_ - s);

    if (auto kw = keyword_of(w)) {
      if (*kw == Punct::KwStyle || *kw == Punct::KwClassDef ||
          *kw == Punct::KwLinkStyle) {
        lex_style_like(*kw, l, c);
        return;
      }
      emit(PunctTok{*kw}, l, c);
      return;
    }
    if (auto d = dir_of(w)) { emit(DirTok{*d}, l, c); return; }

    emit(IdTok{w}, l, c);
    maybe_shape(); // a shape binds only when adjacent (no intervening space)
  }

  void scan_number() {
    const int l = line_, c = col_;
    size_t s = pos_;
    while (is_digit(cur())) adv();
    emit(NumTok{src_.substr(s, pos_ - s)}, l, c);
  }

  // style / classDef / linkStyle: keyword, target, then the rest of the line
  // captured verbatim as a single StyleBody token.
  void lex_style_like(Punct which, int l, int c) {
    emit(PunctTok{which}, l, c);
    skip_inline_ws();

    if (which == Punct::KwLinkStyle) {
      if (starts_with(pos_, "default") && !is_id_char(at(pos_ + 7))) {
        emit(PunctTok{Punct::KwDefault}); adv(7);
      } else {
        while (true) {
          skip_inline_ws();
          if (is_digit(cur())) scan_number();
          else break;
          skip_inline_ws();
          if (cur() == ',') { emit(PunctTok{Punct::Comma}); adv(); continue; }
          break;
        }
      }
    } else {
      // style / classDef target: an identifier, or the `default` keyword.
      size_t s = pos_;
      while (is_id_char(cur())) adv();
      std::string target = src_.substr(s, pos_ - s);
      if (target == "default") emit(PunctTok{Punct::KwDefault});
      else if (!target.empty()) emit(IdTok{target});
    }

    skip_inline_ws();
    size_t s = pos_;
    skip_to_eol();
    std::string body = trim(src_.substr(s, pos_ - s));
    if (!body.empty()) emit(StyleBodyTok{std::move(body)});
  }

  void skip_inline_ws() {
    while (cur() == ' ' || cur() == '\t' || cur() == '\r') adv();
  }

  // ── strings & pipe labels ────────────────────────────────────────────
  void scan_string() {
    const int l = line_, c = col_;
    adv(); // opening "
    size_t s = pos_;
    while (cur() != '\0' && cur() != '"' && cur() != '\n') adv();
    std::string t = src_.substr(s, pos_ - s);
    if (cur() == '"') adv();
    else error("unterminated string");
    emit(StrTok{std::move(t)}, l, c);
  }

  // -->|label|  emits Pipe, Str(label), Pipe
  void scan_pipe_label() {
    emit(PunctTok{Punct::Pipe});
    adv(); // opening |
    size_t s = pos_;
    while (cur() != '\0' && cur() != '|' && cur() != '\n') adv();
    std::string label = clean_label(src_.substr(s, pos_ - s));
    emit(StrTok{std::move(label)});
    if (cur() == '|') { emit(PunctTok{Punct::Pipe}); adv(); }
    else error("unterminated pipe label");
  }

  // ── shapes ───────────────────────────────────────────────────────────
  // Called immediately after an IdTok, with no intervening whitespace.
  void maybe_shape() {
    const char c = cur();
    if (c != '[' && c != '(' && c != '{' && c != '>') return;
    scan_shape();
  }

  void scan_shape() {
    const int l = line_, c0 = col_;
    const char c = cur();
    ShapeKind kind = ShapeKind::Rect;
    int open_len = 1;
    std::array<std::string, 2> closers{}; // up to two candidates
    size_t n_closers = 0;
    auto set = [&](ShapeKind k, int ol, std::initializer_list<const char *> cs) {
      kind = k; open_len = ol; n_closers = 0;
      for (const char *s : cs) closers[n_closers++] = s;
    };

    if (c == '[') {
      const char c1 = at(pos_ + 1);
      if (c1 == '(') set(ShapeKind::Cylinder, 2, {")]"});
      else if (c1 == '[') set(ShapeKind::Subroutine, 2, {"]]"});
      else if (c1 == '/') set(ShapeKind::LeanRight, 2, {"/]", "\\]"});
      else if (c1 == '\\') set(ShapeKind::LeanLeft, 2, {"\\]", "/]"});
      else set(ShapeKind::Rect, 1, {"]"});
    } else if (c == '(') {
      const char c1 = at(pos_ + 1), c2 = at(pos_ + 2);
      if (c1 == '(' && c2 == '(') set(ShapeKind::DoubleCircle, 3, {")))"});
      else if (c1 == '(') set(ShapeKind::Circle, 2, {"))"});
      else if (c1 == '[') set(ShapeKind::Stadium, 2, {"])"});
      else set(ShapeKind::RoundEdges, 1, {")"});
    } else if (c == '{') {
      if (at(pos_ + 1) == '{') set(ShapeKind::Hexagon, 2, {"}}"});
      else set(ShapeKind::Rhombus, 1, {"}"});
    } else if (c == '>') {
      set(ShapeKind::Asymmetric, 1, {"]"});
    } else {
      return;
    }

    const char open0 = at(pos_);
    const char open1 = at(pos_ + 1);
    adv(open_len);

    size_t s = pos_;
    std::string matched;
    while (cur() != '\0' && cur() != '\n') {
      if (cur() == '"') { // skip quoted content so `]` inside can't close
        adv();
        while (cur() != '\0' && cur() != '"' && cur() != '\n') adv();
        if (cur() == '"') adv();
        continue;
      }
      bool done = false;
      for (size_t k = 0; k < n_closers; ++k) {
        if (starts_with(pos_, closers[k])) { matched = closers[k]; done = true; break; }
      }
      if (done) break;
      adv();
    }
    std::string content = src_.substr(s, pos_ - s);

    // Disambiguate lean-vs-trapezoid by which closer actually matched.
    if (open0 == '[' && open1 == '/')
      kind = (matched == "/]") ? ShapeKind::LeanRight : ShapeKind::Trapezoid;
    else if (open0 == '[' && open1 == '\\')
      kind = (matched == "\\]") ? ShapeKind::LeanLeft : ShapeKind::TrapezoidAlt;

    if (!matched.empty()) adv(matched.size());
    else error("unterminated shape");

    emit(ShapeTok{clean_label(content), kind}, l, c0);
  }

  // ── edges ────────────────────────────────────────────────────────────
  // Matches a compact (space-free) operator starting at p (already past any
  // leading '<'). Returns nullopt when p is only the *left half* of a labeled
  // edge (e.g. "--" before " text -->").
  std::optional<OpInfo> match_compact_op(size_t p) const {
    const char c = at(p);
    size_t i = p;

    if (c == '~') {
      while (at(i) == '~') ++i;
      int cnt = static_cast<int>(i - p);
      if (cnt >= 3) return OpInfo{i - p, Stroke::Invisible, ArrowHead::None, cnt - 2};
      return std::nullopt;
    }

    if (c == '=') {
      while (at(i) == '=') ++i;
      int eq = static_cast<int>(i - p);
      if (is_head_char(at(i))) {
        if (eq < 2) return std::nullopt;
        ArrowHead h = head_of(at(i));
        ++i;
        return OpInfo{i - p, Stroke::Thick, h, eq - 1};
      }
      if (eq >= 3) return OpInfo{i - p, Stroke::Thick, ArrowHead::None, eq - 2};
      return std::nullopt;
    }

    if (c == '-') {
      size_t dash0 = i;
      while (at(i) == '-') ++i;
      int d1 = static_cast<int>(i - dash0);

      if (at(i) == '.') { // dotted:  - .+ -+ [head]?
        size_t dot0 = i;
        while (at(i) == '.') ++i;
        int dots = static_cast<int>(i - dot0);
        size_t dash1 = i;
        while (at(i) == '-') ++i;
        if (static_cast<int>(i - dash1) < 1) return std::nullopt;
        ArrowHead h = ArrowHead::None;
        if (is_head_char(at(i))) { h = head_of(at(i)); ++i; }
        return OpInfo{i - p, Stroke::Dotted, h, dots};
      }

      if (is_head_char(at(i))) {
        if (d1 < 2) return std::nullopt;
        ArrowHead h = head_of(at(i));
        ++i;
        return OpInfo{i - p, Stroke::Normal, h, d1 - 1};
      }
      if (d1 >= 3) return OpInfo{i - p, Stroke::Normal, ArrowHead::None, d1 - 2};
      return std::nullopt; // "--" with no head: left half of a labeled edge
    }

    return std::nullopt;
  }

  void scan_edge() {
    const int l = line_, c0 = col_;
    size_t p = pos_;
    bool bidir = false;
    if (at(p) == '<') { bidir = true; ++p; }

    if (auto m = match_compact_op(p)) {
      LinkTok lk;
      lk.stroke = m->stroke;
      lk.head_end = m->head;
      lk.head_start = bidir;
      lk.length = m->length;
      adv((p - pos_) + m->len);
      emit(std::move(lk), l, c0);
      return;
    }

    // Labeled edge: consume the left half, then the label up to the closing op.
    size_t q = p;
    if (at(q) == '=') {
      while (at(q) == '=') ++q;
    } else { // '-'
      while (at(q) == '-') ++q;
      if (at(q) == '.') { while (at(q) == '.') ++q; while (at(q) == '-') ++q; }
    }

    size_t label_start = q;
    std::optional<OpInfo> close;
    size_t r = q;
    while (r < src_.size() && src_[r] != '\n') {
      if (src_[r] == '"') { // skip quoted label text
        ++r;
        while (r < src_.size() && src_[r] != '"' && src_[r] != '\n') ++r;
        if (r < src_.size() && src_[r] == '"') ++r;
        continue;
      }
      if (src_[r] == '-' || src_[r] == '=' || src_[r] == '~') {
        if (auto mc = match_compact_op(r)) { close = mc; break; }
      }
      ++r;
    }

    LinkTok lk;
    lk.head_start = bidir;
    lk.label = clean_label(src_.substr(label_start, r - label_start));
    lk.has_label = !lk.label.empty();
    if (close) {
      lk.stroke = close->stroke;
      lk.head_end = close->head;
      lk.length = close->length;
      adv((r + close->len) - pos_);
    } else {
      lk.stroke = Stroke::Normal;
      lk.head_end = ArrowHead::Arrow;
      lk.length = 1;
      error("unterminated edge label");
      adv(r - pos_);
    }
    emit(std::move(lk), l, c0);
  }

  std::string src_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
  LexResult result_;
};

} // namespace

LexResult lex(std::string_view src) { return Lexer(src).run(); }

// ── to_string (diagnostics / tests) ────────────────────────────────────────
namespace {
const char *dir_name(Direction d) {
  switch (d) {
  case Direction::TB: return "TB";
  case Direction::BT: return "BT";
  case Direction::LR: return "LR";
  case Direction::RL: return "RL";
  }
  return "?";
}
const char *shape_name(ShapeKind s) {
  switch (s) {
  case ShapeKind::Rect: return "Rect";
  case ShapeKind::RoundEdges: return "RoundEdges";
  case ShapeKind::Stadium: return "Stadium";
  case ShapeKind::Subroutine: return "Subroutine";
  case ShapeKind::Cylinder: return "Cylinder";
  case ShapeKind::Circle: return "Circle";
  case ShapeKind::Asymmetric: return "Asymmetric";
  case ShapeKind::Rhombus: return "Rhombus";
  case ShapeKind::Hexagon: return "Hexagon";
  case ShapeKind::LeanRight: return "LeanRight";
  case ShapeKind::LeanLeft: return "LeanLeft";
  case ShapeKind::Trapezoid: return "Trapezoid";
  case ShapeKind::TrapezoidAlt: return "TrapezoidAlt";
  case ShapeKind::DoubleCircle: return "DoubleCircle";
  }
  return "?";
}
const char *stroke_name(Stroke s) {
  switch (s) {
  case Stroke::Normal: return "normal";
  case Stroke::Thick: return "thick";
  case Stroke::Dotted: return "dotted";
  case Stroke::Invisible: return "invisible";
  }
  return "?";
}
const char *head_name(ArrowHead h) {
  switch (h) {
  case ArrowHead::None: return "none";
  case ArrowHead::Arrow: return "arrow";
  case ArrowHead::Circle: return "circle";
  case ArrowHead::Cross: return "cross";
  }
  return "?";
}
const char *punct_name(Punct p) {
  switch (p) {
  case Punct::End: return "End";
  case Punct::Newline: return "Newline";
  case Punct::Ampersand: return "Ampersand";
  case Punct::Pipe: return "Pipe";
  case Punct::Comma: return "Comma";
  case Punct::TripleColon: return "TripleColon";
  case Punct::KwGraph: return "KwGraph";
  case Punct::KwSubgraph: return "KwSubgraph";
  case Punct::KwEnd: return "KwEnd";
  case Punct::KwDirection: return "KwDirection";
  case Punct::KwStyle: return "KwStyle";
  case Punct::KwClassDef: return "KwClassDef";
  case Punct::KwClass: return "KwClass";
  case Punct::KwLinkStyle: return "KwLinkStyle";
  case Punct::KwClick: return "KwClick";
  case Punct::KwDefault: return "KwDefault";
  }
  return "?";
}
} // namespace

std::string to_string(const Token &t) {
  struct Visitor {
    std::string operator()(const PunctTok &p) const { return punct_name(p.punct); }
    std::string operator()(const DirTok &d) const {
      return std::string("Dir(") + dir_name(d.dir) + ")";
    }
    std::string operator()(const NumTok &n) const { return "Num(" + n.text + ")"; }
    std::string operator()(const IdTok &i) const { return "Id(" + i.text + ")"; }
    std::string operator()(const ShapeTok &s) const {
      return std::string("Shape[") + shape_name(s.shape) + ",\"" + s.label + "\"]";
    }
    std::string operator()(const StrTok &s) const { return "Str(\"" + s.text + "\")"; }
    std::string operator()(const StyleBodyTok &s) const {
      return "StyleBody(\"" + s.text + "\")";
    }
    std::string operator()(const LinkTok &lk) const {
      std::string r = std::string("Link{") + stroke_name(lk.stroke) + "," +
                      head_name(lk.head_end) + (lk.head_start ? ",bi" : "") +
                      ",len=" + std::to_string(lk.length);
      if (lk.has_label) r += ",label=\"" + lk.label + "\"";
      return r + "}";
    }
  };
  return std::visit(Visitor{}, t.value);
}

} // namespace mermaid
