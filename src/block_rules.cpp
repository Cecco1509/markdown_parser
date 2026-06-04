#include "markdown_parser/block_rules.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace block_rules {

// ── Internal helpers ─────────────────────────────────────────────────────────

static bool icontains(std::string_view hay, std::string_view needle) {
    if (needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size() && ok; ++j)
            ok = std::tolower(static_cast<unsigned char>(hay[i + j]))
              == std::tolower(static_cast<unsigned char>(needle[j]));
        if (ok) return true;
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

static void stripTrailingBlankLines(std::string& s) {
    while (!s.empty()) {
        // find last newline
        const std::size_t tail = (s.back() == '\n') ? s.size() - 1 : std::string::npos;
        const std::size_t prev = (tail != std::string::npos) ? s.rfind('\n', tail - 1) : std::string::npos;
        const std::size_t line_start = (prev == std::string::npos) ? 0 : prev + 1;
        const std::size_t line_end   = (tail != std::string::npos) ? tail : s.size();
        bool blank = true;
        for (std::size_t i = line_start; i < line_end; ++i)
            if (s[i] != ' ' && s[i] != '\t') { blank = false; break; }
        if (!blank) break;
        s.resize(line_start);
    }
}

// ── §3.1 Continuation ────────────────────────────────────────────────────────

ContinuationResult continuationMatches(const BlockNode& node, const ScannedLine& line,
                                       std::size_t current_col) {
    switch (node.type) {

    case NodeType::Document:
        return {true};

    case NodeType::BlockQuote: {
        // > at 0–3 spaces virtual indent
        if (line.virtual_indent <= 3
                && line.next_non_space < line.content.size()
                && line.content[line.next_non_space] == '>') {
            std::size_t cols = line.virtual_indent + 1; // indent cols + '>'
            const std::size_t after = line.next_non_space + 1;
            if (after < line.content.size()
                    && (line.content[after] == ' ' || line.content[after] == '\t'))
                ++cols;
            return {true, cols};
        }
        return {false};
    }

    case NodeType::List:
        // List itself always continues; its active Item decides.
        return {true};

    case NodeType::Item: {
        const auto& item = std::get<ItemData>(node.data);
        if (line.is_blank)
            return {true};
        if (line.virtual_indent >= static_cast<std::size_t>(item.padding)) {
            // cols_to_consume is relative to current_col (already consumed by
            // parent containers), so subtract what's already been consumed.
            const std::size_t rel = static_cast<std::size_t>(item.padding) - current_col;
            return {true, rel};
        }
        return {false};
    }

    case NodeType::CodeBlock: {
        const auto& cbd = std::get<CodeBlockData>(node.data);
        if (cbd.fenced) {
            // Closing fence: same char, length ≥ opener length, remaining indent
            // (after parent containers consumed current_col columns) ≤ 3.
            if (line.virtual_indent <= current_col + 3) {
                const std::size_t start = line.next_non_space;
                std::size_t run = 0;
                while (start + run < line.content.size()
                        && line.content[start + run] == cbd.fence_char)
                    ++run;
                if (run >= static_cast<std::size_t>(cbd.fence_len)) {
                    bool trailing_ok = true;
                    for (std::size_t j = start + run; j < line.content.size(); ++j) {
                        if (line.content[j] != ' ' && line.content[j] != '\t') {
                            trailing_ok = false; break;
                        }
                    }
                    if (trailing_ok)
                        return {false, 0, /*swallow_line=*/true};
                }
            }
            return {true};
        }
        // Indented code block: blank or remaining indent (after parent containers
        // consumed current_col columns) is still ≥ 4.
        if (line.is_blank)                                    return {true};
        if (line.virtual_indent >= current_col + 4)           return {true, 4};
        return {false};
    }

    case NodeType::Heading:
        // ATX heading: single-line block, never continues.
        return {false};

    case NodeType::HtmlBlock: {
        const auto& hbd = std::get<HtmlBlockData>(node.data);
        if (hbd.html_type == 7) return {false};
        if (hbd.html_type == 6) return {!line.is_blank};
        // Types 1–5: always continue; end condition checked post-append.
        return {true};
    }

    case NodeType::Paragraph:
        return {!line.is_blank};

    case NodeType::ThematicBreak:
        return {false};
    }
    return {false};
}

// ── §3.1 HTML block end detection ────────────────────────────────────────────

bool htmlBlockEndMet(const BlockNode& node, std::string_view line_content) {
    const auto& hbd = std::get<HtmlBlockData>(node.data);
    switch (hbd.html_type) {
    case 1:
        return icontains(line_content, "</script>")
            || icontains(line_content, "</pre>")
            || icontains(line_content, "</style>")
            || icontains(line_content, "</textarea>");
    case 2: return line_content.find("-->")  != std::string_view::npos;
    case 3: return line_content.find("?>")   != std::string_view::npos;
    case 4: return line_content.find('>')    != std::string_view::npos;
    case 5: return line_content.find("]]>")  != std::string_view::npos;
    default: return false;
    }
}

// ── §3.1 Setext underline ─────────────────────────────────────────────────────

bool isSetextUnderline(const ScannedLine& line) {
    if (line.virtual_indent > 3 || line.is_blank) return false;
    const std::size_t i = line.next_non_space;
    if (i >= line.content.size()) return false;
    const char c = line.content[i];
    if (c != '=' && c != '-') return false;
    // All remaining chars must be the same marker char, then optional spaces.
    bool past_marker = false;
    for (std::size_t j = i; j < line.content.size(); ++j) {
        if (!past_marker && line.content[j] == c) continue;
        past_marker = true;
        if (line.content[j] != ' ' && line.content[j] != '\t') return false;
    }
    return true;
}

// ── §3.2 Open — per-type helpers ─────────────────────────────────────────────

// 1. BlockQuote
static std::optional<OpenResult> tryOpenBlockQuote(const ScannedLine& line) {
    if (line.virtual_indent > 3) return std::nullopt;
    if (line.next_non_space >= line.content.size()) return std::nullopt;
    if (line.content[line.next_non_space] != '>') return std::nullopt;
    std::size_t cols = line.virtual_indent + 1;
    const std::size_t after = line.next_non_space + 1;
    if (after < line.content.size()
            && (line.content[after] == ' ' || line.content[after] == '\t'))
        ++cols;
    return OpenResult{NodeType::BlockQuote, std::monostate{}, {}, {}, false, cols};
}

// 2. ATX heading
static std::optional<OpenResult> tryOpenAtxHeading(const ScannedLine& line) {
    if (line.virtual_indent > 3) return std::nullopt;
    const std::string_view s = line.content;
    std::size_t i = line.next_non_space;
    int level = 0;
    while (i < s.size() && s[i] == '#' && level < 7) { ++i; ++level; }
    if (level < 1 || level > 6) return std::nullopt;
    // Must be followed by space/tab or end of line.
    if (i < s.size() && s[i] != ' ' && s[i] != '\t') return std::nullopt;
    // Extract heading text: trim leading/trailing whitespace.
    std::string_view raw = (i < s.size()) ? s.substr(i) : std::string_view{};
    raw = trimLeft(raw);
    raw = trimRight(raw);
    // Strip optional trailing '#' run preceded by space/tab (or empty).
    {
        std::size_t end = raw.size();
        while (end > 0 && raw[end - 1] == '#') --end;
        if (end < raw.size()) {
            if (end == 0 || raw[end - 1] == ' ' || raw[end - 1] == '\t')
                raw = trimRight(raw.substr(0, end));
        }
    }
    return OpenResult{NodeType::Heading, HeadingData{level, false},
                      {}, std::string(raw), /*swallow_line=*/true, 0};
}

// 3. Fenced code block
static std::optional<OpenResult> tryOpenFencedCode(const ScannedLine& line) {
    if (line.virtual_indent > 3) return std::nullopt;
    const std::string_view s = line.content;
    const std::size_t i = line.next_non_space;
    if (i >= s.size()) return std::nullopt;
    const char fc = s[i];
    if (fc != '`' && fc != '~') return std::nullopt;
    std::size_t run = 0;
    while (i + run < s.size() && s[i + run] == fc) ++run;
    if (run < 3) return std::nullopt;
    std::string_view info = s.substr(i + run);
    info = trimLeft(trimRight(info));
    // Backtick fence: info string must not contain a backtick.
    if (fc == '`' && info.find('`') != std::string_view::npos) return std::nullopt;
    CodeBlockData cbd{true, fc, static_cast<int>(run),
                      static_cast<int>(line.virtual_indent), std::string(info)};
    return OpenResult{NodeType::CodeBlock, cbd, {}, {}, /*swallow_line=*/true, 0};
}

// 4. HTML block
static const char* const kType1Tags[] = {"pre", "script", "style", "textarea", nullptr};
static const char* const kType6Tags[] = {
    "address","article","aside","base","basefont","blockquote","body",
    "caption","center","col","colgroup","dd","details","dialog","dir",
    "div","dl","dt","fieldset","figcaption","figure","footer","form",
    "frame","frameset","h1","h2","h3","h4","h5","h6","head","header",
    "hr","html","iframe","legend","li","link","main","menu","menuitem",
    "nav","noframes","ol","optgroup","option","p","param","picture",
    "search","section","summary","table","tbody","td","tfoot","th",
    "thead","title","tr","track","ul", nullptr
};

static bool matchesTagList(std::string_view tag, const char* const* list) {
    for (; *list; ++list) {
        const std::size_t n = std::strlen(*list);
        if (tag.size() != n) continue;
        bool ok = true;
        for (std::size_t j = 0; j < n && ok; ++j)
            ok = std::tolower(static_cast<unsigned char>(tag[j])) == static_cast<unsigned char>((*list)[j]);
        if (ok) return true;
    }
    return false;
}

static bool isTagNameEnd(char c) {
    return c == ' ' || c == '\t' || c == '>' || c == '/';
}

static std::optional<OpenResult> tryOpenHtmlBlock(const ScannedLine& line, bool tip_is_paragraph) {
    if (line.virtual_indent > 3) return std::nullopt;
    const std::string_view s = line.content;
    const std::size_t i = line.next_non_space;
    if (i >= s.size() || s[i] != '<') return std::nullopt;

    const std::string_view rest = s.substr(i);
    auto make = [](int t) { return OpenResult{NodeType::HtmlBlock, HtmlBlockData{t}}; };

    // Type 2: <!--
    if (rest.size() >= 4 && rest[1] == '!' && rest[2] == '-' && rest[3] == '-')
        return make(2);
    // Type 3: <?
    if (rest.size() >= 2 && rest[1] == '?') return make(3);
    // Type 5: <![CDATA[
    if (rest.size() >= 9 && rest.substr(0, 9) == "<![CDATA[") return make(5);
    // Type 4: <! followed by ASCII uppercase letter
    if (rest.size() >= 3 && rest[1] == '!'
            && std::isupper(static_cast<unsigned char>(rest[2])))
        return make(4);

    // Extract tag name for type-1 and type-6 checks.
    if (rest.size() < 2) return std::nullopt;
    const bool closing = (rest[1] == '/');
    std::size_t name_start = closing ? 2 : 1;
    std::size_t name_end   = name_start;
    while (name_end < rest.size()
            && std::isalnum(static_cast<unsigned char>(rest[name_end])))
        ++name_end;
    if (name_end == name_start) return std::nullopt;
    const std::string_view tag = rest.substr(name_start, name_end - name_start);
    // Next char must be space, '/', '>', or end-of-string.
    if (name_end < rest.size() && !isTagNameEnd(rest[name_end]))
        return std::nullopt;

    // Type 1: <pre, <script, <style, <textarea  (no closing tag as opener)
    if (!closing && matchesTagList(tag, kType1Tags)) return make(1);

    // Type 6: block-level tags (open or close)
    if (matchesTagList(tag, kType6Tags)) return make(6);

    // Type 7: complete open/close tag, cannot interrupt a paragraph.
    // Full regex matching is deferred; basic heuristic only.
    // TODO: implement full CommonMark §4.6 type-7 pattern.
    if (!tip_is_paragraph) {
        // Simplified type-7: close tag with no attributes followed by optional spaces.
        if (closing) {
            std::size_t j = name_end;
            while (j < rest.size() && (rest[j] == ' ' || rest[j] == '\t')) ++j;
            if (j < rest.size() && rest[j] == '>') {
                ++j;
                bool trail_ok = true;
                while (j < rest.size()) {
                    if (rest[j] != ' ' && rest[j] != '\t') { trail_ok = false; break; }
                    ++j;
                }
                if (trail_ok) return make(7);
            }
        }
    }

    return std::nullopt;
}

// 5. Thematic break
static std::optional<OpenResult> tryOpenThematicBreak(const ScannedLine& line) {
    if (line.virtual_indent > 3) return std::nullopt;
    const std::string_view s = line.content;
    const std::size_t i = line.next_non_space;
    if (i >= s.size()) return std::nullopt;
    const char c = s[i];
    if (c != '*' && c != '-' && c != '_') return std::nullopt;
    int count = 0;
    for (std::size_t j = i; j < s.size(); ++j) {
        if (s[j] == c)                              { ++count; continue; }
        if (s[j] == ' ' || s[j] == '\t')           continue;
        return std::nullopt;
    }
    if (count < 3) return std::nullopt;
    return OpenResult{NodeType::ThematicBreak, std::monostate{}, {}, {}, /*swallow_line=*/true, 0};
}

// 6. List item / List
static std::optional<OpenResult> tryOpenListItem(const ScannedLine& line, bool tip_is_paragraph) {
    if (line.virtual_indent > 3) return std::nullopt;
    const std::string_view s = line.content;
    const std::size_t i = line.next_non_space;
    if (i >= s.size()) return std::nullopt;

    ListType  ltype   = ListType::Bullet;
    char      bullet  = 0;
    int       start   = 1;
    ListDelim delim   = ListDelim::Period;
    std::size_t marker_end = i;

    const char c = s[i];
    if (c == '-' || c == '*' || c == '+') {
        ltype      = ListType::Bullet;
        bullet     = c;
        marker_end = i + 1;
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
        ltype = ListType::Ordered;
        std::size_t j = i;
        int num = 0, digits = 0;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])) && digits < 9) {
            num = num * 10 + (s[j] - '0'); ++j; ++digits;
        }
        if (digits == 0 || j >= s.size()) return std::nullopt;
        if (s[j] != '.' && s[j] != ')') return std::nullopt;
        delim      = (s[j] == ')') ? ListDelim::Paren : ListDelim::Period;
        start      = num;
        marker_end = j + 1;
        // Paragraph interruption: ordered marker start must be 1.
        if (tip_is_paragraph && start != 1) return std::nullopt;
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
        int col = static_cast<int>(line.virtual_indent) + marker_width;
        while (content_start < s.size()
                && (s[content_start] == ' ' || s[content_start] == '\t')) {
            if (s[content_start] == '\t') {
                const int tab_w = (col / 4 + 1) * 4 - col;
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
    if (tip_is_paragraph && empty_item) return std::nullopt;

    // padding (absolute) = absolute_marker_col + marker_width + spaces, capped
    // at marker+1 for empty items or excessive leading spaces (> 4).
    // Stored in ItemData for continuation matching (compared against the
    // original line's virtual_indent which is also absolute from col 0).
    // cols_consumed is relative to the current scan start (base_col), i.e. the
    // number of columns to advance from the current position.
    const int abs_marker_col = static_cast<int>(line.base_col + line.virtual_indent);
    const int capped_spaces  = (empty_item || spaces > 4) ? 1 : spaces;
    const int padding        = abs_marker_col + marker_width + capped_spaces;
    const std::size_t cols_consumed =
        static_cast<std::size_t>(static_cast<int>(line.virtual_indent) + marker_width + capped_spaces);

    ItemData item_data{static_cast<int>(line.virtual_indent), padding};
    ListData list_data{ltype, bullet, start, delim, /*tight=*/true, padding};

    return OpenResult{NodeType::Item, item_data, list_data, {},
                      false, cols_consumed};
}

// 7. Indented code block
static std::optional<OpenResult> tryOpenIndentedCode(const ScannedLine& line,
                                                      bool tip_is_paragraph,
                                                      bool inside_list_blank) {
    if (tip_is_paragraph) return std::nullopt;
    if (line.virtual_indent < 4) return std::nullopt;
    if (line.is_blank) return std::nullopt;
    return OpenResult{NodeType::CodeBlock,
                      CodeBlockData{false, 0, 0, 0, {}},
                      {}, {}, false, 4};
}

// ── §3.2 Public entry point ───────────────────────────────────────────────────

std::optional<OpenResult> tryOpen(const ScannedLine& line,
                                  bool tip_is_paragraph,
                                  bool inside_list_blank) {
    if (line.is_blank) return std::nullopt;

    if (auto r = tryOpenBlockQuote(line))                           return r;
    if (auto r = tryOpenAtxHeading(line))                           return r;
    if (auto r = tryOpenFencedCode(line))                           return r;
    if (auto r = tryOpenHtmlBlock(line, tip_is_paragraph))          return r;
    if (auto r = tryOpenThematicBreak(line))                        return r;
    if (auto r = tryOpenListItem(line, tip_is_paragraph))           return r;
    if (auto r = tryOpenIndentedCode(line, tip_is_paragraph,
                                     inside_list_blank))            return r;
    return std::nullopt;
}

// ── §3.3 Close (finalization hook) ───────────────────────────────────────────

void onClose(BlockNode& node) {
    switch (node.type) {
    case NodeType::CodeBlock: {
        const auto& cbd = std::get<CodeBlockData>(node.data);
        if (!cbd.fenced) stripTrailingBlankLines(node.string_content);
        break;
    }
    case NodeType::Heading: {
        const auto& hd = std::get<HeadingData>(node.data);
        if (hd.setext) {
            std::string_view sv = node.string_content;
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
