#include "markdown_parser/block_rules.hpp"
#include "markdown_parser/commonmark_constants.hpp"
#include "markdown_parser/entities.hpp"
#include "markdown_parser/string_utils.hpp"
#include <cctype>
#include <cstring>
#include <iostream>

namespace markdown_parser {
namespace block_rules {

namespace {
constexpr std::size_t kMaxHeadingLevel = 6;
constexpr std::size_t kMaxListDigits = 9;
constexpr std::size_t kFenceMinRun = 3;
} // namespace

// ── Internal helpers ─────────────────────────────────────────────────────────

static bool icontains(std::string_view hay, std::string_view needle) {
  if (needle.size() > hay.size())
    return false;
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < needle.size() && ok; ++j)
      ok = std::tolower(static_cast<unsigned char>(hay[i + j])) ==
           std::tolower(static_cast<unsigned char>(needle[j]));
    if (ok)
      return true;
  }
  return false;
}

static std::string_view trimRight(std::string_view s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);
  return s;
}

static std::string_view trimLeft(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  return s;
}

static void stripTrailingBlankLines(std::string &s) {
  while (!s.empty()) {
    // find last newline
    const std::size_t tail =
        (s.back() == '\n') ? s.size() - 1 : std::string::npos;
    const std::size_t prev = (tail != std::string::npos)
                                 ? s.rfind('\n', tail - 1)
                                 : std::string::npos;
    const std::size_t line_start = (prev == std::string::npos) ? 0 : prev + 1;
    const std::size_t line_end = (tail != std::string::npos) ? tail : s.size();
    bool blank = true;
    for (std::size_t i = line_start; i < line_end; ++i)
      if (s[i] != ' ' && s[i] != '\t') {
        blank = false;
        break;
      }
    if (!blank)
      break;
    s.resize(line_start);
  }
}

// ── §3.1 Continuation ────────────────────────────────────────────────────────

ContinuationResult continuationMatches(const BlockNode &node,
                                       const ScannedLine &line,
                                       std::size_t current_col, bool debug) {
#define CONT_LOG(matched, ...)                                                 \
  do {                                                                         \
    if (debug)                                                                 \
      std::cerr << "  CONT " << nodeTypeToString(node.type)                    \
                << (matched ? " ✓  " : " ✗  ") << __VA_ARGS__ << "\n";         \
  } while (0)

  switch (node.type) {

  case NodeType::Document:
    CONT_LOG(true, "always");
    return {true};

  case NodeType::BlockQuote: {
    if (line.indent() <= commonmark::kMaxBlockIndent &&
        line.next_non_space() < line.content().size() &&
        line.content()[line.next_non_space()] == '>') {
      std::size_t cols = line.indent() + 1;
      const std::size_t after = line.next_non_space() + 1;
      if (after < line.content().size()) {
        if (line.content()[after] == ' ') {
          ++cols;
        } else if (line.content()[after] == '\t') {
          ++cols; // take only 1 virtual space from the tab
        }
      }
      CONT_LOG(true, "indent=" << line.indent() << " '>' at pos "
                               << line.next_non_space()
                               << "  consume=" << cols);
      return {true, cols};
    }
    if (line.indent() > commonmark::kMaxBlockIndent)
      CONT_LOG(false, "indent=" << line.indent() << " > kMaxBlockIndent("
                                << commonmark::kMaxBlockIndent << ")");
    else
      CONT_LOG(false, "no '>' at next_non_space pos " << line.next_non_space());
    return {false};
  }

  case NodeType::List:
    CONT_LOG(true, "always (Item decides)");
    return {true};

  case NodeType::Item: {
    const auto &item = std::get<ItemData>(node.data);
    if (line.is_blank()) {
      CONT_LOG(true, "blank line (items absorb blanks)");
      return {true};
    }
    if (node.last_line_blank && node.children.empty()) {
      CONT_LOG(false, "empty item after blank  last_line_blank=1 children=0");
      return {false};
    }
    if (line.indent() >= static_cast<std::size_t>(item.padding)) {
      CONT_LOG(true, "indent=" << line.indent() << " >= padding="
                               << item.padding << "  consume=" << item.padding);
      return {true, static_cast<std::size_t>(item.padding)};
    }
    CONT_LOG(false,
             "indent=" << line.indent() << " < padding=" << item.padding);
    return {false};
  }

  case NodeType::CodeBlock: {
    const auto &cbd = std::get<CodeBlockData>(node.data);
    if (cbd.fenced) {
      if (line.indent() <= current_col + 3) {
        const std::size_t start = line.next_non_space();
        std::size_t run = 0;
        while (start + run < line.content().size() &&
               line.content()[start + run] == cbd.fence_char)
          ++run;
        if (run >= static_cast<std::size_t>(cbd.fence_len)) {
          bool trailing_ok = true;
          for (std::size_t j = start + run; j < line.content().size(); ++j) {
            if (line.content()[j] != ' ' && line.content()[j] != '\t') {
              trailing_ok = false;
              break;
            }
          }
          if (trailing_ok) {
            CONT_LOG(false, "closing fence: run=" << run << " >= fence_len="
                                                  << cbd.fence_len
                                                  << "  swallow");
            return {false, 0, /*swallow_line=*/true};
          }
        }
      }
      CONT_LOG(true, "fenced (continues until closing fence)");
      return {true};
    }
    // Indented code block
    if (line.is_blank()) {
      CONT_LOG(true, "blank inside indented code block");
      return {true};
    }
    if (line.indent() >= commonmark::kCodeBlockIndent) {
      CONT_LOG(true, "indent=" << line.indent()
                               << " >= 4=" << (commonmark::kCodeBlockIndent)
                               << "  consume=4");
      return {true, 4};
    }
    CONT_LOG(false, "indent=" << line.indent() << " < base+4="
                              << (current_col + commonmark::kCodeBlockIndent));
    return {false};
  }

  case NodeType::Heading:
    CONT_LOG(false, "ATX heading is single-line");
    return {false};

  case NodeType::HtmlBlock: {
    const auto &hbd = std::get<HtmlBlockData>(node.data);
    if (hbd.html_type == HtmlBlockType::Complete ||
        hbd.html_type == HtmlBlockType::KnownTag) {
      const bool ok = !line.is_blank();
      CONT_LOG(ok, "type=6/7 (KnownTag/Complete)  blank=" << line.is_blank());
      return {ok};
    }
    CONT_LOG(true, "type=1-5 (end condition checked post-append)");
    return {true};
  }

  case NodeType::Paragraph: {
    const bool ok = !line.is_blank();
    CONT_LOG(ok, "blank=" << line.is_blank());
    return {ok};
  }

  case NodeType::ThematicBreak:
    CONT_LOG(false, "ThematicBreak is single-line");
    return {false};
  }
  return {false};

#undef CONT_LOG
}

// ── §3.1 HTML block end detection ────────────────────────────────────────────

bool htmlBlockEndMet(const BlockNode &node, std::string_view line_content) {
  const auto &hbd = std::get<HtmlBlockData>(node.data);
  switch (hbd.html_type) {
  case HtmlBlockType::ScriptStylePre:
    return icontains(line_content, "</script>") ||
           icontains(line_content, "</pre>") ||
           icontains(line_content, "</style>") ||
           icontains(line_content, "</textarea>");
  case HtmlBlockType::Comment:
    return line_content.find("-->") != std::string_view::npos;
  case HtmlBlockType::ProcessingInstr:
    return line_content.find("?>") != std::string_view::npos;
  case HtmlBlockType::Declaration:
    return line_content.find('>') != std::string_view::npos;
  case HtmlBlockType::CData:
    return line_content.find("]]>") != std::string_view::npos;
  default:
    return false;
  }
}

// ── §3.1 Setext underline
// ─────────────────────────────────────────────────────

bool isSetextUnderline(const ScannedLine &line) {
  // std::cerr << "[isSetextUnderline] content=\"" << line.content()
  //           << "\" virtual_indent=" << line.indent()
  //           << " is_blank=" << line.is_blank() << "\n";
  if (line.indent() > commonmark::kMaxBlockIndent || line.is_blank()) {
    // std::cerr << "[isSetextUnderline] -> false (indent/blank)\n";
    return false;
  }
  const std::size_t i = line.next_non_space();
  if (i >= line.content().size()) {
    // std::cerr << "[isSetextUnderline] -> false (empty after indent)\n";
    return false;
  }
  const char c = line.content()[i];
  if (c != '=' && c != '-') {
    // std::cerr << "[isSetextUnderline] -> false (char='" << c << "' not = or
    // -)\n";
    return false;
  }
  // All remaining chars must be the same marker char, then optional spaces.
  bool past_marker = false;
  for (std::size_t j = i; j < line.content().size(); ++j) {
    if (!past_marker && line.content()[j] == c)
      continue;
    past_marker = true;
    if (line.content()[j] != ' ' && line.content()[j] != '\t') {
      // std::cerr << "[isSetextUnderline] -> false (mixed chars)\n";
      return false;
    }
  }
  // std::cerr << "[isSetextUnderline] -> true (marker='" << c << "')\n";
  return true;
}

// ── §3.2 Open — per-type helpers ─────────────────────────────────────────────

// 1. BlockQuote
static std::optional<OpenResult> tryOpenBlockQuote(const ScannedLine &line) {
  if (line.indent() > commonmark::kMaxBlockIndent)
    return std::nullopt;
  if (line.next_non_space() >= line.content().size())
    return std::nullopt;
  if (line.content()[line.next_non_space()] != '>')
    return std::nullopt;
  std::size_t cols = line.indent() + 1;
  const std::size_t after = line.next_non_space() + 1;
  if (after < line.content().size()) {
    if (line.content()[after] == ' ') {
      ++cols;
    } else if (line.content()[after] == '\t') {
      ++cols; // take only 1 virtual space from the tab; remainder becomes
              // prefix_spaces
    }
  }
  return OpenResult{
      NodeType::BlockQuote, std::monostate{}, {}, {}, false, cols};
}

// 2. ATX heading
static std::optional<OpenResult> tryOpenAtxHeading(const ScannedLine &line) {
  if (line.indent() > commonmark::kMaxBlockIndent)
    return std::nullopt;
  const std::string_view s = line.content();
  std::size_t i = line.next_non_space();
  int level = 0;
  while (i < s.size() && s[i] == '#' && level <= kMaxHeadingLevel) {
    ++i;
    ++level;
  }
  if (level < 1 || level > 6)
    return std::nullopt;
  // Must be followed by space/tab or end of line.
  if (i < s.size() && s[i] != ' ' && s[i] != '\t')
    return std::nullopt;
  // Extract heading text: trim leading/trailing whitespace.
  std::string_view raw = (i < s.size()) ? s.substr(i) : std::string_view{};
  raw = trimLeft(raw);
  raw = trimRight(raw);
  // Strip optional trailing '#' run preceded by space/tab (or empty).
  {
    std::size_t end = raw.size();
    while (end > 0 && raw[end - 1] == '#')
      --end;
    if (end < raw.size()) {
      if (end == 0 || raw[end - 1] == ' ' || raw[end - 1] == '\t')
        raw = trimRight(raw.substr(0, end));
    }
  }
  return OpenResult{NodeType::Heading, HeadingData{level, false}, {},
                    std::string(raw),  /*swallow_line=*/true,     0};
}

// 3. Fenced code block
static std::optional<OpenResult> tryOpenFencedCode(const ScannedLine &line) {
  if (line.indent() > commonmark::kMaxBlockIndent)
    return std::nullopt;
  const std::string_view s = line.content();
  const std::size_t i = line.next_non_space();
  if (i >= s.size())
    return std::nullopt;
  const char fc = s[i];
  if (fc != '`' && fc != '~')
    return std::nullopt;
  std::size_t run = 0;
  while (i + run < s.size() && s[i + run] == fc)
    ++run;
  if (run < kFenceMinRun)
    return std::nullopt;
  std::string_view info = s.substr(i + run);
  info = trimLeft(trimRight(info));
  // Backtick fence: info string must not contain a backtick.
  if (fc == '`' && info.find('`') != std::string_view::npos)
    return std::nullopt;
  std::string info_str = string_utils::processEscapesAndEntities(info);
  CodeBlockData cbd{true, fc, static_cast<int>(run),
                    static_cast<int>(line.indent()), std::move(info_str)};
  return OpenResult{NodeType::CodeBlock, cbd, {}, {}, /*swallow_line=*/true, 0};
}

// 4. HTML block
static const char *const kType1Tags[] = {"pre", "script", "style", "textarea",
                                         nullptr};
static const char *const kType6Tags[] = {
    "address",  "article",    "aside",  "base",     "basefont", "blockquote",
    "body",     "caption",    "center", "col",      "colgroup", "dd",
    "details",  "dialog",     "dir",    "div",      "dl",       "dt",
    "fieldset", "figcaption", "figure", "footer",   "form",     "frame",
    "frameset", "h1",         "h2",     "h3",       "h4",       "h5",
    "h6",       "head",       "header", "hr",       "html",     "iframe",
    "legend",   "li",         "link",   "main",     "menu",     "menuitem",
    "nav",      "noframes",   "ol",     "optgroup", "option",   "p",
    "param",    "picture",    "search", "section",  "summary",  "table",
    "tbody",    "td",         "tfoot",  "th",       "thead",    "title",
    "tr",       "track",      "ul",     nullptr};

static bool matchesTagList(std::string_view tag, const char *const *list) {
  for (; *list; ++list) {
    const std::size_t n = std::strlen(*list);
    if (tag.size() != n)
      continue;
    bool ok = true;
    for (std::size_t j = 0; j < n && ok; ++j)
      ok = std::tolower(static_cast<unsigned char>(tag[j])) ==
           static_cast<unsigned char>((*list)[j]);
    if (ok)
      return true;
  }
  return false;
}

static bool isTagNameEnd(char c) {
  return c == ' ' || c == '\t' || c == '>' || c == '/';
}

// Returns true if rest[pos..] parses as zero-or-more attributes followed by
// optional whitespace, optional '/', '>', and only whitespace until end.
// Used for CommonMark §4.6 type-7 complete open tag detection.
static bool isCompleteOpenTagSuffix(std::string_view rest, std::size_t pos) {
  while (pos < rest.size()) {
    char c = rest[pos];
    if (c == '>') {
      ++pos;
      while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t'))
        ++pos;
      return pos >= rest.size();
    }
    if (c == '/') {
      if (pos + 1 < rest.size() && rest[pos + 1] == '>') {
        pos += 2;
        while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t'))
          ++pos;
        return pos >= rest.size();
      }
      return false;
    }
    if (c != ' ' && c != '\t')
      return false;
    // Skip whitespace before next attribute or '>'/'/'
    while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t'))
      ++pos;
    if (pos >= rest.size())
      return false;
    c = rest[pos];
    if (c == '>' || c == '/')
      continue; // handled on next iteration
    // Attribute name: must start with letter, '_', or ':'
    if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_' && c != ':')
      return false;
    ++pos;
    while (pos < rest.size()) {
      char ac = rest[pos];
      if (std::isalnum(static_cast<unsigned char>(ac)) || ac == '_' ||
          ac == '.' || ac == ':' || ac == '-')
        ++pos;
      else
        break;
    }
    // Optional '=' followed by attribute value
    if (pos < rest.size() && rest[pos] == '=') {
      ++pos;
      if (pos >= rest.size())
        return false;
      char vc = rest[pos];
      if (vc == '"') {
        ++pos;
        while (pos < rest.size() && rest[pos] != '"')
          ++pos;
        if (pos >= rest.size())
          return false;
        ++pos;
      } else if (vc == '\'') {
        ++pos;
        while (pos < rest.size() && rest[pos] != '\'')
          ++pos;
        if (pos >= rest.size())
          return false;
        ++pos;
      } else {
        // Unquoted value: no whitespace, '"', '\'', '=', '<', '>', '`'
        if (vc == ' ' || vc == '\t' || vc == '"' || vc == '\'' || vc == '=' ||
            vc == '<' || vc == '>' || vc == '`')
          return false;
        while (pos < rest.size()) {
          char uv = rest[pos];
          if (uv == ' ' || uv == '\t' || uv == '"' || uv == '\'' || uv == '=' ||
              uv == '<' || uv == '>' || uv == '`')
            break;
          ++pos;
        }
      }
    }
  }
  return false; // never reached '>'
}

static std::optional<OpenResult> tryOpenHtmlBlock(const ScannedLine &line,
                                                  bool tip_is_paragraph) {
  if (line.indent() > commonmark::kMaxBlockIndent)
    return std::nullopt;
  const std::string_view s = line.content();
  const std::size_t i = line.next_non_space();
  if (i >= s.size() || s[i] != '<')
    return std::nullopt;

  const std::string_view rest = s.substr(i);
  auto make = [](HtmlBlockType t) {
    return OpenResult{NodeType::HtmlBlock, HtmlBlockData{t}};
  };

  // Type 2: <!--
  if (rest.size() >= 4 && rest[1] == '!' && rest[2] == '-' && rest[3] == '-')
    return make(HtmlBlockType::Comment);
  // Type 3: <?
  if (rest.size() >= 2 && rest[1] == '?')
    return make(HtmlBlockType::ProcessingInstr);
  // Type 5: <![CDATA[
  if (rest.size() >= 9 && rest.substr(0, 9) == "<![CDATA[")
    return make(HtmlBlockType::CData);
  // Type 4: <! followed by ASCII uppercase letter
  if (rest.size() >= 3 && rest[1] == '!' &&
      std::isupper(static_cast<unsigned char>(rest[2])))
    return make(HtmlBlockType::Declaration);

  // Extract tag name for type-1 and type-6 checks.
  if (rest.size() < 2)
    return std::nullopt;
  const bool closing = (rest[1] == '/');
  std::size_t name_start = closing ? 2 : 1;
  std::size_t name_end = name_start;
  while (name_end < rest.size() &&
         std::isalnum(static_cast<unsigned char>(rest[name_end])))
    ++name_end;
  if (name_end == name_start)
    return std::nullopt;
  const std::string_view tag = rest.substr(name_start, name_end - name_start);
  // Next char must be space, '/', '>', or end-of-string.
  if (name_end < rest.size() && !isTagNameEnd(rest[name_end]))
    return std::nullopt;

  // Type 1: <pre, <script, <style, <textarea  (no closing tag as opener)
  if (!closing && matchesTagList(tag, kType1Tags))
    return make(HtmlBlockType::ScriptStylePre);

  // Type 6: block-level tags (open or close)
  if (matchesTagList(tag, kType6Tags))
    return make(HtmlBlockType::KnownTag);

  // Type 7: complete open/close tag, cannot interrupt a paragraph.
  if (!tip_is_paragraph) {
    bool complete = false;
    if (closing) {
      // Close tag: </tagname ws* > (no attributes allowed)
      std::size_t j = name_end;
      while (j < rest.size() && (rest[j] == ' ' || rest[j] == '\t'))
        ++j;
      if (j < rest.size() && rest[j] == '>') {
        ++j;
        while (j < rest.size() && (rest[j] == ' ' || rest[j] == '\t'))
          ++j;
        complete = (j >= rest.size());
      }
    } else if (!matchesTagList(tag, kType1Tags)) {
      // Open tag: <tagname attrs* ws* /? > — not pre/script/style/textarea
      complete = isCompleteOpenTagSuffix(rest, name_end);
    }
    if (complete)
      return make(HtmlBlockType::Complete);
  }

  return std::nullopt;
}

// 5. Thematic break
static std::optional<OpenResult> tryOpenThematicBreak(const ScannedLine &line) {
  if (line.indent() > commonmark::kMaxBlockIndent)
    return std::nullopt;
  const std::string_view s = line.content();
  const std::size_t i = line.next_non_space();
  if (i >= s.size())
    return std::nullopt;
  const char c = s[i];
  if (c != '*' && c != '-' && c != '_')
    return std::nullopt;
  int count = 0;
  for (std::size_t j = i; j < s.size(); ++j) {
    if (s[j] == c) {
      ++count;
      continue;
    }
    if (s[j] == ' ' || s[j] == '\t')
      continue;
    return std::nullopt;
  }
  if (count < 3)
    return std::nullopt;
  return OpenResult{NodeType::ThematicBreak,
                    std::monostate{},
                    {},
                    {},
                    /*swallow_line=*/true,
                    0};
}

// 6. List item / List
static std::optional<OpenResult> tryOpenListItem(const ScannedLine &line,
                                                 bool tip_is_paragraph) {
  if (line.indent() > commonmark::kMaxBlockIndent)
    return std::nullopt;
  const std::string_view s = line.content();
  const std::size_t i = line.next_non_space();
  if (i >= s.size())
    return std::nullopt;

  ListType ltype = ListType::Bullet;
  char bullet = 0;
  int start = 1;
  ListDelim delim = ListDelim::Period;
  std::size_t marker_end = i;

  const char c = s[i];
  if (c == '-' || c == '*' || c == '+') {
    ltype = ListType::Bullet;
    bullet = c;
    marker_end = i + 1;
  } else if (std::isdigit(static_cast<unsigned char>(c))) {
    ltype = ListType::Ordered;
    std::size_t j = i;
    int num = 0, digits = 0;
    while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])) &&
           digits < kMaxListDigits) {
      num = num * 10 + (s[j] - '0');
      ++j;
      ++digits;
    }
    if (digits == 0 || j >= s.size())
      return std::nullopt;
    if (s[j] != '.' && s[j] != ')')
      return std::nullopt;
    delim = (s[j] == ')') ? ListDelim::Paren : ListDelim::Period;
    start = num;
    marker_end = j + 1;
    // Paragraph interruption: ordered marker start must be 1.
    if (tip_is_paragraph && start != 1)
      return std::nullopt;
  } else {
    return std::nullopt;
  }

  // Marker must be followed by space/tab, or be at end-of-line (empty item).
  if (marker_end < s.size() && s[marker_end] != ' ' && s[marker_end] != '\t')
    return std::nullopt;

  const int marker_width = static_cast<int>(marker_end - i);
  // Count visual columns of whitespace after the marker (tabs expand to tab
  // stops of 4). The starting column is marker_col + marker_width.
  int spaces = 0;
  std::size_t content_start = marker_end;
  {
    int col = static_cast<int>(line.indent()) + marker_width;
    while (content_start < s.size() &&
           (s[content_start] == ' ' || s[content_start] == '\t')) {
      if (s[content_start] == '\t') {
        const int tab_w =
            (col / commonmark::kTabStop + 1) * commonmark::kTabStop - col;
        spaces += tab_w;
        col += tab_w;
      } else {
        ++spaces;
        ++col;
      }
      ++content_start;
    }
  }

  const bool empty_item = (content_start >= s.size());
  // Paragraph interruption: empty list item cannot interrupt a paragraph.
  if (tip_is_paragraph && empty_item)
    return std::nullopt;

  // padding (relative) = marker_offset_within_container + marker_width +
  // capped_spaces. Relative to where the item opens, not the line start.
  // This makes continuation matching independent of how many cols parent
  // containers consumed on the continuation line vs. the opening line.
  const int capped_spaces = (empty_item || spaces > 4) ? 1 : spaces;
  const int padding =
      static_cast<int>(line.indent()) + marker_width + capped_spaces;
  const std::size_t cols_consumed = static_cast<std::size_t>(
      static_cast<int>(line.indent()) + marker_width + capped_spaces);

  ItemData item_data{static_cast<int>(line.indent()), padding};
  ListData list_data{ltype, bullet, start, delim, /*tight=*/true, padding};

  return OpenResult{NodeType::Item, item_data,    list_data, {},
                    false,          cols_consumed};
}

// 7. Indented code block
static std::optional<OpenResult> tryOpenIndentedCode(const ScannedLine &line,
                                                     bool tip_is_paragraph,
                                                     bool inside_list_blank) {
  // Print line info for debugging
  // std::cerr << "[tryOpenIndentedCode] content=\"" << line.content()
  //           << "\" virtual_indent=" << line.indent()
  //           << " is_blank=" << line.is_blank()
  //           << " tip_is_paragraph=" << tip_is_paragraph
  //           << " inside_list_blank=" << inside_list_blank << "\n";

  if (tip_is_paragraph)
    return std::nullopt;
  if (line.indent() < commonmark::kCodeBlockIndent)
    return std::nullopt;
  if (line.is_blank())
    return std::nullopt;
  return OpenResult{
      NodeType::CodeBlock, CodeBlockData{false, 0, 0, 0, {}}, {}, {}, false, 4};
}

// ── §3.2 Public entry point
// ───────────────────────────────────────────────────

std::optional<OpenResult> tryOpen(const ScannedLine &line,
                                  bool tip_is_paragraph,
                                  bool inside_list_blank) {
  if (line.is_blank())
    return std::nullopt;

  if (auto r = tryOpenBlockQuote(line))
    return r;
  if (auto r = tryOpenAtxHeading(line))
    return r;
  if (auto r = tryOpenFencedCode(line))
    return r;
  if (auto r = tryOpenHtmlBlock(line, tip_is_paragraph))
    return r;
  if (auto r = tryOpenThematicBreak(line))
    return r;
  if (auto r = tryOpenListItem(line, tip_is_paragraph))
    return r;
  if (auto r = tryOpenIndentedCode(line, tip_is_paragraph, inside_list_blank))
    return r;
  return std::nullopt;
}

// ── §3.3 Close (finalization hook) ───────────────────────────────────────────

void onClose(BlockNode &node) {
  switch (node.type) {
  case NodeType::CodeBlock: {
    const auto &cbd = std::get<CodeBlockData>(node.data);
    if (!cbd.fenced)
      stripTrailingBlankLines(node.string_content);
    break;
  }
  case NodeType::Heading: {
    const auto &hd = std::get<HeadingData>(node.data);
    if (hd.setext) {
      // Strip trailing whitespace from every line, then trim leading/trailing
      // newlines from the whole content (CommonMark: trailing spaces on setext
      // heading content lines are insignificant).
      std::string &sc = node.string_content;
      std::string result;
      result.reserve(sc.size());
      std::size_t pos = 0;
      while (pos < sc.size()) {
        std::size_t nl = sc.find('\n', pos);
        std::string_view seg = (nl == std::string::npos)
                                   ? std::string_view(sc).substr(pos)
                                   : std::string_view(sc).substr(pos, nl - pos);
        while (!seg.empty() && (seg.back() == ' ' || seg.back() == '\t'))
          seg.remove_suffix(1);
        result.append(seg);
        if (nl != std::string::npos) {
          result += '\n';
          pos = nl + 1;
        } else {
          break;
        }
      }
      std::string_view sv = result;
      while (!sv.empty() && (sv.front() == '\n' || sv.front() == '\r'))
        sv.remove_prefix(1);
      while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r'))
        sv.remove_suffix(1);
      node.string_content = std::string(sv);
    }
    break;
  }
  default:
    break;
  }
}

} // namespace block_rules
} // namespace markdown_parser
