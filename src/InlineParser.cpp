#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/entities.hpp"
#include "markdown_parser/string_utils.hpp"
#include "markdown_parser/unicode_fold.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>

// ── file-local helpers
// ────────────────────────────────────────────────────────

static bool isAsciiPunct(char c) {
  auto u = static_cast<unsigned char>(c);
  return (u >= '!' && u <= '/') || (u >= ':' && u <= '@') ||
         (u >= '[' && u <= '`') || (u >= '{' && u <= '~');
}

// ── Unicode helpers for flanking rules ───────────────────────────────────────

// Decode the UTF-8 codepoint starting at s[pos]; returns U+FFFD on error.
static uint32_t cpAt(std::string_view s, std::size_t pos) {
  if (pos >= s.size()) return 0;
  auto b0 = static_cast<unsigned char>(s[pos]);
  if (b0 < 0x80) return b0;
  auto cont = [&](std::size_t i) -> uint32_t {
    return (i < s.size()) ? (static_cast<unsigned char>(s[i]) & 0x3F) : 0;
  };
  if (b0 < 0xE0) return ((b0 & 0x1F) << 6) | cont(pos + 1);
  if (b0 < 0xF0) return ((b0 & 0x0F) << 12) | (cont(pos + 1) << 6) | cont(pos + 2);
  return ((b0 & 0x07) << 18) | (cont(pos + 1) << 12) | (cont(pos + 2) << 6) | cont(pos + 3);
}

// Walk backward past continuation bytes to find the start of the last codepoint
// that ends at or before pos, then decode it.
static uint32_t cpBefore(std::string_view s, std::size_t pos) {
  if (pos == 0) return 0;
  std::size_t p = pos - 1;
  while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
    --p;
  return cpAt(s, p);
}

// Unicode Zs (space separator) + ASCII control whitespace.
static bool isUnicodeWhitespaceCp(uint32_t cp) {
  if (cp == 0x09 || cp == 0x0A || cp == 0x0C || cp == 0x0D || cp == 0x20) return true;
  if (cp == 0x00A0 || cp == 0x1680) return true;
  if (cp >= 0x2000 && cp <= 0x200A) return true;
  if (cp == 0x202F || cp == 0x205F || cp == 0x3000) return true;
  return false;
}

// Unicode P (punctuation) and S (symbol) general categories.
// Table generated from Unicode 15 data; ranges are [lo, hi] inclusive.
static const uint32_t kUniPunctRanges[][2] = {
    {0x0021, 0x002F}, {0x003A, 0x0040}, {0x005B, 0x0060}, {0x007B, 0x007E},
    {0x00A1, 0x00A9}, {0x00AB, 0x00AC}, {0x00AE, 0x00B1}, {0x00B4, 0x00B4},
    {0x00B6, 0x00B8}, {0x00BB, 0x00BB}, {0x00BF, 0x00BF}, {0x00D7, 0x00D7},
    {0x00F7, 0x00F7}, {0x02C2, 0x02C5}, {0x02D2, 0x02DF}, {0x02E5, 0x02EB},
    {0x02ED, 0x02ED}, {0x02EF, 0x02FF}, {0x0375, 0x0375}, {0x037E, 0x037E},
    {0x0384, 0x0385}, {0x0387, 0x0387}, {0x03F6, 0x03F6}, {0x0482, 0x0482},
    {0x055A, 0x055F}, {0x0589, 0x058A}, {0x058D, 0x058F}, {0x05BE, 0x05BE},
    {0x05C0, 0x05C0}, {0x05C3, 0x05C3}, {0x05C6, 0x05C6}, {0x05F3, 0x05F4},
    {0x0606, 0x060F}, {0x061B, 0x061B}, {0x061D, 0x061F}, {0x066A, 0x066D},
    {0x06D4, 0x06D4}, {0x06E9, 0x06E9}, {0x06FD, 0x06FE}, {0x0700, 0x070D},
    {0x07F6, 0x07F9}, {0x07FE, 0x07FF}, {0x0830, 0x083E}, {0x085E, 0x085E},
    {0x0888, 0x0888}, {0x0964, 0x0965}, {0x0970, 0x0970}, {0x09F2, 0x09F3},
    {0x09FA, 0x09FB}, {0x09FD, 0x09FD}, {0x0A76, 0x0A76}, {0x0AF0, 0x0AF1},
    {0x0B70, 0x0B70}, {0x0BF3, 0x0BFA}, {0x0C77, 0x0C77}, {0x0C7F, 0x0C7F},
    {0x0C84, 0x0C84}, {0x0D4F, 0x0D4F}, {0x0D79, 0x0D79}, {0x0DF4, 0x0DF4},
    {0x0E3F, 0x0E3F}, {0x0E4F, 0x0E4F}, {0x0E5A, 0x0E5B}, {0x0F01, 0x0F17},
    {0x0F1A, 0x0F1F}, {0x0F34, 0x0F34}, {0x0F36, 0x0F36}, {0x0F38, 0x0F38},
    {0x0F3A, 0x0F3D}, {0x0F85, 0x0F85}, {0x0FBE, 0x0FC5}, {0x0FC7, 0x0FCC},
    {0x0FCE, 0x0FCF}, {0x0FD5, 0x0FD8}, {0x109E, 0x109F}, {0x10FB, 0x10FB},
    {0x1360, 0x1368}, {0x1390, 0x1399}, {0x1400, 0x1400}, {0x166D, 0x166E},
    {0x169B, 0x169C}, {0x16EB, 0x16ED}, {0x1735, 0x1736}, {0x17D4, 0x17D6},
    {0x17D8, 0x17DB}, {0x1800, 0x180A}, {0x1940, 0x1940}, {0x1944, 0x1945},
    {0x19DE, 0x19FF}, {0x1A1E, 0x1A1F}, {0x1AA0, 0x1AA6}, {0x1AA8, 0x1AAD},
    {0x1B5A, 0x1B6A}, {0x1B74, 0x1B7E}, {0x1BFC, 0x1BFF}, {0x1C3B, 0x1C3F},
    {0x1C7E, 0x1C7F}, {0x1CC0, 0x1CC7}, {0x1CD3, 0x1CD3}, {0x1FBD, 0x1FBD},
    {0x1FBF, 0x1FC1}, {0x1FCD, 0x1FCF}, {0x1FDD, 0x1FDF}, {0x1FED, 0x1FEF},
    {0x1FFD, 0x1FFE}, {0x2010, 0x2027}, {0x2030, 0x205E}, {0x2060, 0x2064},
    {0x2066, 0x2070}, {0x207A, 0x207E}, {0x208A, 0x208E}, {0x20A0, 0x20C0},
    {0x2100, 0x2101}, {0x2103, 0x2106}, {0x2108, 0x2109}, {0x2114, 0x2114},
    {0x2116, 0x2118}, {0x211E, 0x2123}, {0x2125, 0x2125}, {0x2127, 0x2127},
    {0x2129, 0x2129}, {0x212E, 0x212E}, {0x213A, 0x213B}, {0x2140, 0x2144},
    {0x214A, 0x214D}, {0x214F, 0x214F}, {0x218A, 0x218B}, {0x2190, 0x2426},
    {0x2440, 0x244A}, {0x249C, 0x24E9}, {0x2500, 0x2775}, {0x2794, 0x27C4},
    {0x27C7, 0x27E5}, {0x27F0, 0x2982}, {0x2999, 0x29D7}, {0x29DC, 0x29FB},
    {0x29FE, 0x2B73}, {0x2B76, 0x2B95}, {0x2B97, 0x2BFF}, {0x2CE5, 0x2CEA},
    {0x2CF9, 0x2CFC}, {0x2CFE, 0x2CFF}, {0x2D70, 0x2D70}, {0x2E00, 0x2E5D},
    {0x2E80, 0x2E99}, {0x2E9B, 0x2EF3}, {0x2F00, 0x2FD5}, {0x2FF0, 0x2FFB},
    {0x3001, 0x3004}, {0x3008, 0x3020}, {0x3030, 0x3030}, {0x3036, 0x3037},
    {0x303D, 0x303F}, {0x309B, 0x309C}, {0x30A0, 0x30A0}, {0x30FB, 0x30FB},
    {0x3190, 0x3191}, {0x3196, 0x319F}, {0x31C0, 0x31E3}, {0x31EF, 0x31EF},
    {0x3200, 0x321E}, {0x3220, 0x3247}, {0x3260, 0x327E}, {0x327F, 0x32CF},
    {0x3358, 0x33FF}, {0x4DC0, 0x4DFF}, {0xA490, 0xA4C6}, {0xA4FE, 0xA4FF},
    {0xA60D, 0xA60F}, {0xA673, 0xA673}, {0xA67E, 0xA67F}, {0xA6F2, 0xA6F7},
    {0xA700, 0xA716}, {0xA720, 0xA721}, {0xA789, 0xA78A}, {0xA828, 0xA82B},
    {0xA836, 0xA839}, {0xA874, 0xA877}, {0xA8CE, 0xA8CF}, {0xA8F8, 0xA8FA},
    {0xA8FC, 0xA8FC}, {0xA92E, 0xA92F}, {0xA95F, 0xA95F}, {0xA9C1, 0xA9CD},
    {0xA9DE, 0xA9DF}, {0xAA5C, 0xAA5F}, {0xAA77, 0xAA79}, {0xAADE, 0xAADF},
    {0xAAF0, 0xAAF1}, {0xAB5B, 0xAB5B}, {0xAB6A, 0xAB6B}, {0xABEB, 0xABEB},
    {0xFB29, 0xFB29}, {0xFBB2, 0xFBC2}, {0xFD3E, 0xFD3F}, {0xFD40, 0xFD4F},
    {0xFDCF, 0xFDCF}, {0xFDFC, 0xFDFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE52},
    {0xFE54, 0xFE66}, {0xFE68, 0xFE6B}, {0xFF01, 0xFF0F}, {0xFF1A, 0xFF20},
    {0xFF3B, 0xFF40}, {0xFF5B, 0xFF65}, {0xFF70, 0xFF70}, {0xFF9E, 0xFF9F},
    {0xFFE0, 0xFFE6}, {0xFFE8, 0xFFEE}, {0xFFF9, 0xFFFD},
    // Supplementary planes (selected ranges)
    {0x10100, 0x10102}, {0x10137, 0x1013F}, {0x10179, 0x10189},
    {0x1018C, 0x1018E}, {0x10190, 0x1019C}, {0x101A0, 0x101A0},
    {0x101D0, 0x101FC}, {0x10877, 0x10878}, {0x10AC8, 0x10AC8},
    {0x1173F, 0x1173F}, {0x11FD5, 0x11FF1}, {0x16B3C, 0x16B3F},
    {0x16B45, 0x16B45}, {0x1BC9C, 0x1BC9C}, {0x1D000, 0x1D0F5},
    {0x1D100, 0x1D126}, {0x1D129, 0x1D164}, {0x1D16A, 0x1D16C},
    {0x1D183, 0x1D184}, {0x1D18C, 0x1D1A9}, {0x1D1AE, 0x1D1EA},
    {0x1D1ED, 0x1D1FF}, {0x1D200, 0x1D241}, {0x1D245, 0x1D245},
    {0x1D300, 0x1D356}, {0x1D6C1, 0x1D6C1}, {0x1D6DB, 0x1D6DB},
    {0x1D6FB, 0x1D6FB}, {0x1D715, 0x1D715}, {0x1D735, 0x1D735},
    {0x1D74F, 0x1D74F}, {0x1D76F, 0x1D76F}, {0x1D789, 0x1D789},
    {0x1D7A9, 0x1D7A9}, {0x1D7C3, 0x1D7C3}, {0x1D800, 0x1DA8B},
    {0x1DA9B, 0x1DA9F}, {0x1DAA1, 0x1DAAF}, {0x1E14F, 0x1E14F},
    {0x1ECAC, 0x1ECAC}, {0x1ECB0, 0x1ECB0}, {0x1ED2E, 0x1ED2E},
    {0x1EEF0, 0x1EEF1}, {0x1F000, 0x1F02B}, {0x1F030, 0x1F093},
    {0x1F0A0, 0x1F0AE}, {0x1F0B1, 0x1F0BF}, {0x1F0C1, 0x1F0CF},
    {0x1F0D1, 0x1F0F5}, {0x1F10D, 0x1F1AD}, {0x1F1E0, 0x1F1FF},
    {0x1F201, 0x1F202}, {0x1F210, 0x1F23B}, {0x1F240, 0x1F248},
    {0x1F250, 0x1F251}, {0x1F300, 0x1F6D7}, {0x1F6DC, 0x1F6EC},
    {0x1F6F0, 0x1F6FC}, {0x1F700, 0x1F776}, {0x1F77B, 0x1F7D9},
    {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F800, 0x1F80B},
    {0x1F810, 0x1F847}, {0x1F850, 0x1F859}, {0x1F860, 0x1F887},
    {0x1F890, 0x1F8AD}, {0x1F8B0, 0x1F8B1}, {0x1F900, 0x1FA53},
    {0x1FA60, 0x1FA6D}, {0x1FA70, 0x1FA74}, {0x1FA78, 0x1FA7C},
    {0x1FA80, 0x1FA86}, {0x1FA90, 0x1FAAC}, {0x1FAB0, 0x1FABA},
    {0x1FAC0, 0x1FAC5}, {0x1FAD0, 0x1FAD9}, {0x1FAE0, 0x1FAE7},
    {0x1FAF0, 0x1FAF6},
};
static const std::size_t kUniPunctRangesLen =
    sizeof(kUniPunctRanges) / sizeof(kUniPunctRanges[0]);

static bool isUnicodePunctCp(uint32_t cp) {
  std::size_t lo = 0, hi = kUniPunctRangesLen;
  while (lo < hi) {
    std::size_t mid = (lo + hi) / 2;
    if      (cp < kUniPunctRanges[mid][0]) hi = mid;
    else if (cp > kUniPunctRanges[mid][1]) lo = mid + 1;
    else                                   return true;
  }
  return false;
}

// ── parse (entry point)
// ───────────────────────────────────────────────────────

void InlineParser::parse(
    BlockNode *block, const std::unordered_map<std::string, LinkDef> &ref_map) {
  input_ = block->string_content;
  while (!input_.empty() && input_.back() == '\n')
    input_.remove_suffix(1);
  pos_ = 0;
  ref_map_ = &ref_map;
  delimiters_.clear();
  brackets_.clear();
  nodes_.clear();
  // Upper bound: one node per byte → no reallocation, keeping raw ptrs stable.
  nodes_.reserve(input_.size() + 1);

  while (pos_ < input_.size()) {
    auto node = parseInline();
    if (node)
      nodes_.push_back(std::move(node));
  }

  processEmphasis(std::nullopt);

  if (nodes_.size() > 0 && nodes_.back()->type == InlineType::Text &&
      !string_utils::isBlank(nodes_.back()->literal))
    nodes_.back()->literal = string_utils::trimRight(nodes_.back()->literal);

  block->inline_children = std::move(nodes_);
}

// ── parseInline (dispatcher)
// ──────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseInline() {
  char c = input_[pos_];

  // ── newline: soft break or hard line break (spec §6.7) ───────────────────
  if (c == '\n') {
    ++pos_;
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t'))
      ++pos_;
    // Check last text node for hard-break conditions.
    if (!nodes_.empty() && nodes_.back()->type == InlineType::Text) {
      std::string &lit = nodes_.back()->literal;
      if (lit.size() >= 2 && lit[lit.size() - 1] == ' ' &&
          lit[lit.size() - 2] == ' ') {
        while (!lit.empty() && lit.back() == ' ')
          lit.pop_back();
        return makeNode(InlineType::LineBreak);
      }
      if (!lit.empty() && lit.back() == '\\') {
        lit.pop_back();
        return makeNode(InlineType::LineBreak);
      }
    }
    if (!nodes_.empty() && nodes_.back()->type == InlineType::Text)
      nodes_.back()->literal = string_utils::trimRight(nodes_.back()->literal);
    return makeNode(InlineType::SoftBreak);
  }

  // ── backslash escape / hard break (spec §6.9) ────────────────────────────
  if (c == '\\') {
    ++pos_;
    if (pos_ < input_.size()) {
      char next = input_[pos_];
      if (next == '\n') {
        ++pos_;
        while (pos_ < input_.size() &&
               (input_[pos_] == ' ' || input_[pos_] == '\t'))
          ++pos_;
        return makeNode(InlineType::LineBreak);
      }
      if (isAsciiPunct(next)) {
        ++pos_;
        auto node = makeNode(InlineType::Text);
        node->literal = std::string(1, next);
        return node;
      }
    }
    auto node = makeNode(InlineType::Text);
    node->literal = "\\";
    return node;
  }

  // ── backtick code span (spec §6.1) ───────────────────────────────────────
  if (c == '`')
    return parseBacktickString();

  // ── emphasis / strong (spec §6.4) ────────────────────────────────────────
  if (c == '*' || c == '_') {
    std::size_t start = pos_;
    while (pos_ < input_.size() && input_[pos_] == c)
      ++pos_;
    std::size_t run_len = pos_ - start;
    auto node = makeNode(InlineType::Text);
    node->literal = std::string(input_.substr(start, run_len));
    std::size_t prev_sz = delimiters_.size();
    handleEmphasis(c, run_len);
    if (delimiters_.size() > prev_sz)
      delimiters_.back().node = node.get();
    return node;
  }

  // ── image opener / bare '!' ──────────────────────────────────────────────
  if (c == '!') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '[') {
      pos_ += 2;
      auto node = makeNode(InlineType::Text);
      node->literal = "![";
      handleBracketOpener(true);
      if (!brackets_.empty())
        brackets_.back().node = node.get();
      return node;
    }
    // Bare '!': emit as literal and advance, or it would loop forever.
    ++pos_;
    auto node = makeNode(InlineType::Text);
    node->literal = "!";
    return node;
  }

  // ── link opener ──────────────────────────────────────────────────────────
  if (c == '[') {
    ++pos_;
    auto node = makeNode(InlineType::Text);
    node->literal = "[";
    handleBracketOpener(false);
    if (!brackets_.empty())
      brackets_.back().node = node.get();
    return node;
  }

  // ── bracket closer ───────────────────────────────────────────────────────
  if (c == ']') {
    ++pos_;
    return handleBracketCloser();
  }

  // ── autolink or inline HTML ───────────────────────────────────────────────
  if (c == '<') {
    if (auto n = parseAutolink())
      return n;
    if (auto n = parseHtmlInline())
      return n;
    ++pos_;
    auto node = makeNode(InlineType::Text);
    node->literal = "<";
    return node;
  }

  // ── entity reference (spec §2.5) ─────────────────────────────────────────
  if (c == '&') {
    std::size_t save = pos_;
    std::string decoded = entities::decode(input_, pos_);
    if (!decoded.empty()) {
      auto node = makeNode(InlineType::Text);
      node->literal = std::move(decoded);
      return node;
    }
    // Not a valid entity; emit '&' as literal text.
    pos_ = save + 1;
    auto node = makeNode(InlineType::Text);
    node->literal = "&";
    return node;
  }

  // ── regular text run ─────────────────────────────────────────────────────
  std::size_t start = pos_;
  while (pos_ < input_.size()) {
    char ch = input_[pos_];
    if (ch == '\n' || ch == '\\' || ch == '`' || ch == '*' || ch == '_' ||
        ch == '[' || ch == ']' || ch == '!' || ch == '<' || ch == '&')
      break;
    ++pos_;
  }
  auto node = makeNode(InlineType::Text);
  node->literal = std::string(input_.substr(start, pos_ - start));
  return node;
}

// ── parseBacktickString (spec §6.1) ──────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseBacktickString() {
  std::size_t tick_start = pos_;
  while (pos_ < input_.size() && input_[pos_] == '`')
    ++pos_;
  std::size_t tick_len = pos_ - tick_start;

  // Search for matching closing run of the same length.
  std::size_t search = pos_;
  while (search < input_.size()) {
    if (input_[search] == '`') {
      std::size_t cs = search;
      while (search < input_.size() && input_[search] == '`')
        ++search;
      if (search - cs == tick_len) {
        // Found. Normalise content.
        std::string content(input_.substr(pos_, cs - pos_));
        for (char &ch : content)
          if (ch == '\n')
            ch = ' ';
        bool all_spaces = content.find_first_not_of(' ') == std::string::npos;
        if (!all_spaces && content.size() >= 2 && content.front() == ' ' &&
            content.back() == ' ')
          content = content.substr(1, content.size() - 2);
        pos_ = search;
        auto node = makeNode(InlineType::Code);
        node->literal = std::move(content);
        return node;
      }
    } else {
      ++search;
    }
  }
  // No matching close: emit opening ticks as literal text.
  auto node = makeNode(InlineType::Text);
  node->literal = std::string(tick_len, '`');
  return node;
}

// ── parseAutolink (spec §6.7)
// ─────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseAutolink() {
  std::size_t save = pos_;
  ++pos_; // skip '<'
  std::size_t inner = pos_;

  // URI autolink: <scheme:path>
  // scheme: letter followed by letters/digits/+/-/. up to 32 chars total
  std::size_t sp = pos_;
  if (sp < input_.size() &&
      std::isalpha(static_cast<unsigned char>(input_[sp]))) {
    while (sp < input_.size() && sp - pos_ < 32) {
      char sc = input_[sp];
      if (std::isalpha(static_cast<unsigned char>(sc)) ||
          (sp > pos_ && (std::isdigit(static_cast<unsigned char>(sc)) ||
                         sc == '+' || sc == '-' || sc == '.')))
        ++sp;
      else
        break;
    }
    if (sp - pos_ >= 2 && sp < input_.size() && input_[sp] == ':') {
      std::size_t up = sp + 1;
      while (up < input_.size() && input_[up] != '>' && input_[up] != '<' &&
             input_[up] != ' ' && static_cast<unsigned char>(input_[up]) > 0x1F)
        ++up;
      if (up < input_.size() && input_[up] == '>') {
        std::string dest(input_.substr(inner, up - inner));
        pos_ = up + 1;
        auto link = makeNode(InlineType::Link);
        link->data = LinkData{dest, std::nullopt, std::nullopt};
        auto text = makeNode(InlineType::Text);
        text->literal = dest;
        link->children.push_back(std::move(text));
        return link;
      }
    }
  }

  // Email autolink: <local@domain>
  pos_ = inner;
  std::size_t at = pos_;
  while (at < input_.size() && input_[at] != '@' && input_[at] != '>' &&
         input_[at] != '\n')
    ++at;
  if (at < input_.size() && input_[at] == '@' && at > pos_) {
    bool local_ok = true;
    for (std::size_t i = pos_; i < at && local_ok; ++i) {
      char ch = input_[i];
      if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' ||
            ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
            ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '/' ||
            ch == '=' || ch == '?' || ch == '^' || ch == '_' || ch == '`' ||
            ch == '{' || ch == '|' || ch == '}' || ch == '~'))
        local_ok = false;
    }
    if (local_ok) {
      std::size_t dp = at + 1;
      bool domain_ok = true;
      std::size_t lbl = dp;
      while (dp < input_.size() && input_[dp] != '>' && input_[dp] != '\n') {
        char ch = input_[dp];
        if (ch == '.') {
          if (dp == lbl) {
            domain_ok = false;
            break;
          }
          lbl = dp + 1;
        } else if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') {
          domain_ok = false;
          break;
        }
        ++dp;
      }
      if (domain_ok && dp < input_.size() && input_[dp] == '>' && dp > at + 1 &&
          dp != lbl) {
        std::string email(input_.substr(inner, dp - inner));
        pos_ = dp + 1;
        auto link = makeNode(InlineType::Link);
        link->data = LinkData{"mailto:" + email, std::nullopt, std::nullopt};
        auto text = makeNode(InlineType::Text);
        text->literal = email;
        link->children.push_back(std::move(text));
        return link;
      }
    }
  }

  pos_ = save;
  return nullptr;
}

// ── parseHtmlInline (spec §6.8)
// ───────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseHtmlInline() {
  std::size_t save = pos_;
  ++pos_; // skip '<'

  // std::cerr << "parseHtmlInline at pos " << save << ": " << input_ <<
  // "...\n";

  if (pos_ >= input_.size()) {
    pos_ = save;
    return nullptr;
  }
  char c = input_[pos_];

  auto emit = [&]() -> std::unique_ptr<InlineNode> {
    auto node = makeNode(InlineType::HtmlInline);
    node->literal = std::string(input_.substr(save, pos_ - save));
    return node;
  };

  // Closing tag: </tagname>
  if (c == '/') {
    ++pos_;
    if (pos_ >= input_.size() ||
        !std::isalpha(static_cast<unsigned char>(input_[pos_]))) {
      pos_ = save;
      // std::cerr << "Not a closing tag: " << input_ << "...\n";
      return nullptr;
    }
    while (pos_ < input_.size() &&
           (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
            input_[pos_] == '-'))
      ++pos_;
    while (pos_ < input_.size() && input_[pos_] == ' ')
      ++pos_;
    if (pos_ < input_.size() && input_[pos_] == '>') {
      ++pos_;
      return emit();
    }
    pos_ = save;
    // std::cerr << "Malformed closing tag: " << input_ << "...\n";
    return nullptr;
  }

  // ! variants: comment, CDATA, declaration
  if (c == '!') {
    // HTML comment per spec 0.31.2:
    //   "<!-->", "<!--->", or "<!--" + text not containing "-->" + "-->"
    if (pos_ + 2 < input_.size() && input_[pos_ + 1] == '-' &&
        input_[pos_ + 2] == '-') {
      pos_ += 3; // now past "<!--"
      // "<!-->": the very next char closes it.
      if (pos_ < input_.size() && input_[pos_] == '>') {
        ++pos_;
        return emit();
      }
      // "<!--->": dash then close.
      if (pos_ + 1 < input_.size() && input_[pos_] == '-' &&
          input_[pos_ + 1] == '>') {
        pos_ += 2;
        return emit();
      }
      // General: scan until "-->" (content may contain "--" but not "-->").
      while (pos_ + 2 < input_.size()) {
        if (input_[pos_] == '-' && input_[pos_ + 1] == '-' &&
            input_[pos_ + 2] == '>') {
          pos_ += 3;
          return emit();
        }
        ++pos_;
      }
      pos_ = save;
      return nullptr; // unclosed comment
    }
    // CDATA: <![CDATA[...]]>
    if (pos_ + 7 < input_.size() && input_.substr(pos_, 8) == "![CDATA[") {
      pos_ += 8;
      while (pos_ + 2 < input_.size()) {
        if (input_[pos_] == ']' && input_[pos_ + 1] == ']' &&
            input_[pos_ + 2] == '>') {
          pos_ += 3;
          return emit();
        }
        ++pos_;
      }
      pos_ = save;
      return nullptr;
    }
    // Declaration: <!NAME ...>
    if (pos_ + 1 < input_.size() &&
        std::isupper(static_cast<unsigned char>(input_[pos_ + 1]))) {
      ++pos_; // skip '!'
      while (pos_ < input_.size() &&
             std::isupper(static_cast<unsigned char>(input_[pos_])))
        ++pos_;
      while (pos_ < input_.size() && input_[pos_] != '>') {
        if (input_[pos_] == '\n') {
          pos_ = save;
          return nullptr;
        }
        ++pos_;
      }
      if (pos_ < input_.size() && input_[pos_] == '>') {
        ++pos_;
        return emit();
      }
    }
    pos_ = save;
    return nullptr;
  }

  // Processing instruction: <?...?>
  if (c == '?') {
    ++pos_;
    while (pos_ + 1 < input_.size()) {
      if (input_[pos_] == '?' && input_[pos_ + 1] == '>') {
        pos_ += 2;
        return emit();
      }
      ++pos_;
    }
    pos_ = save;
    return nullptr;
  }

  // Open tag: <tagname attributes? /?>
  if (!std::isalpha(static_cast<unsigned char>(c))) {
    pos_ = save;
    // std::cerr << "Tag name must start with a letter: " << input_ << "...\n";
    return nullptr;
  }

  while (pos_ < input_.size() &&
         (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
          input_[pos_] == '-'))
    ++pos_;

  while (pos_ < input_.size()) {
    char ch = input_[pos_];
    if (ch == '>' || ch == '/')
      break;

    // Require at least one whitespace before each attribute.
    if (ch != ' ' && ch != '\t' && ch != '\n') {
      pos_ = save;
      return nullptr;
    }
    while (
        pos_ < input_.size() &&
        (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n'))
      ++pos_;
    if (pos_ >= input_.size()) {
      pos_ = save;
      // std::cerr << "Unexpected end of input in tag: " << input_ << "...\n";
      return nullptr;
    }
    ch = input_[pos_];
    if (ch == '>' || ch == '/')
      break;

    // Attribute name.
    if (!std::isalpha(static_cast<unsigned char>(ch)) && ch != '_' &&
        ch != ':') {
      pos_ = save;
      // std::cerr << "Invalid attribute name start: " << input_ << "...\n";
      return nullptr;
    }
    while (pos_ < input_.size() &&
           (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
            input_[pos_] == '_' || input_[pos_] == ':' || input_[pos_] == '.' ||
            input_[pos_] == '-'))
      ++pos_;

    // Optional = value.
    std::size_t eq_save = pos_;
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t'))
      ++pos_;
    if (pos_ < input_.size() && input_[pos_] == '=') {
      ++pos_;
      while (pos_ < input_.size() &&
             (input_[pos_] == ' ' || input_[pos_] == '\t'))
        ++pos_;
      if (pos_ >= input_.size()) {
        pos_ = save;
        // std::cerr << "Unexpected end of input after '=': " << input_ <<
        // "...\n";
        return nullptr;
      }
      char q = input_[pos_];
      if (q == '"' || q == '\'') {
        ++pos_;
        while (pos_ < input_.size() && input_[pos_] != q) {
          ++pos_;
        }
        if (pos_ >= input_.size()) {
          pos_ = save;
          return nullptr;
        }
        ++pos_;
      } else if (q != ' ' && q != '\t' && q != '"' && q != '\'' && q != '=' &&
                 q != '<' && q != '>' && q != '`' && q != '\n') {
        while (pos_ < input_.size() && input_[pos_] != ' ' &&
               input_[pos_] != '\t' && input_[pos_] != '\n' &&
               input_[pos_] != '"' && input_[pos_] != '\'' &&
               input_[pos_] != '=' && input_[pos_] != '<' &&
               input_[pos_] != '>' && input_[pos_] != '`')
          ++pos_;
      } else {
        pos_ = save;
        // std::cerr << "Invalid attribute value start: " << input_ << "...\n";
        return nullptr;
      }
    } else {
      pos_ = eq_save;
    }
  }

  if (pos_ >= input_.size()) {
    pos_ = save;
    // std::cerr << "Unexpected end of input in tag: " << input_ << "...\n";
    return nullptr;
  }
  if (input_[pos_] == '/') {
    ++pos_;
    if (pos_ >= input_.size() || input_[pos_] != '>') {
      pos_ = save;
      // std::cerr << "Expected '>' after '/': " << input_ << "...\n";
      return nullptr;
    }
  }
  if (input_[pos_] == '>') {
    ++pos_;
    return emit();
  }
  pos_ = save;
  // std::cerr << "Expected '>' at end of tag: " << input_ << "...\n";
  return nullptr;
}

// ── handleBracketOpener
// ───────────────────────────────────────────────────────

void InlineParser::handleBracketOpener(bool is_image) {
  brackets_.push_back({is_image, nullptr, delimiters_.size(), pos_});
}

// ── scanLinkDestination
// ───────────────────────────────────────────────────────

std::optional<std::string> InlineParser::scanLinkDestination() {
  if (pos_ >= input_.size())
    return std::nullopt;
  std::size_t save = pos_;

  if (input_[pos_] == '<') {
    ++pos_;
    std::string dest;
    while (pos_ < input_.size() && input_[pos_] != '\n') {
      char c = input_[pos_];
      if (c == '>') {
        ++pos_;
        return dest;
      }
      if (c == '<') {
        pos_ = save;
        return std::nullopt;
      }
      if (c == '\\' && pos_ + 1 < input_.size() &&
          isAsciiPunct(input_[pos_ + 1])) {
        dest += input_[pos_ + 1];
        pos_ += 2;
      } else if (c == '&') {
        std::string decoded = entities::decode(input_, pos_);
        if (!decoded.empty())
          dest += decoded;
        else {
          dest += c;
          ++pos_;
        }
      } else {
        dest += c;
        ++pos_;
      }
    }
    pos_ = save;
    return std::nullopt;
  }

  // Bare form: balanced parens, no ASCII control chars.
  std::string dest;
  int depth = 0;
  while (pos_ < input_.size()) {
    char c = input_[pos_];
    if (static_cast<unsigned char>(c) <= 0x1F || c == ' ')
      break;
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      if (depth == 0)
        break;
      --depth;
    } else if (c == '\\' && pos_ + 1 < input_.size() &&
               isAsciiPunct(input_[pos_ + 1])) {
      dest += input_[pos_ + 1];
      pos_ += 2;
      continue;
    } else if (c == '&') {
      std::string decoded = entities::decode(input_, pos_);
      if (!decoded.empty()) {
        dest += decoded;
        continue;
      }
    }
    dest += c;
    ++pos_;
  }
  if (depth != 0) {
    pos_ = save;
    return std::nullopt;
  }
  return dest;
}

// ── scanLinkTitle
// ─────────────────────────────────────────────────────────────

std::optional<std::string> InlineParser::scanLinkTitle() {
  if (pos_ >= input_.size())
    return std::nullopt;
  std::size_t save = pos_;
  char open = input_[pos_];
  char close;
  if (open == '"')
    close = '"';
  else if (open == '\'')
    close = '\'';
  else if (open == '(')
    close = ')';
  else
    return std::nullopt;
  ++pos_;

  std::string title;
  while (pos_ < input_.size()) {
    char c = input_[pos_];
    if (c == close) {
      ++pos_;
      return title;
    }
    if (open == '(' && c == '(') {
      pos_ = save;
      return std::nullopt;
    }
    if (c == '\\' && pos_ + 1 < input_.size() &&
        isAsciiPunct(input_[pos_ + 1])) {
      title += input_[pos_ + 1];
      pos_ += 2;
    } else if (c == '&') {
      std::string decoded = entities::decode(input_, pos_);
      if (!decoded.empty())
        title += decoded;
      else {
        title += c;
        ++pos_;
      }
    } else {
      title += c;
      ++pos_;
    }
  }
  pos_ = save;
  return std::nullopt;
}

// ── handleBracketCloser
// ───────────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::handleBracketCloser() {
  auto literal_bracket = [&]() -> std::unique_ptr<InlineNode> {
    auto node = makeNode(InlineType::Text);
    node->literal = "]";
    return node;
  };

  if (brackets_.empty())
    return literal_bracket();

  BracketEntry bracket = brackets_.back();

  // Deactivated openers (set to nullptr when a containing link was resolved).
  if (bracket.node == nullptr) {
    brackets_.pop_back();
    return literal_bracket();
  }

  // Helper: collect literal text of nodes after the bracket opener node.
  auto get_bracket_text = [&]() -> std::string {
    auto op_it = std::find_if(nodes_.begin(), nodes_.end(), [&](const auto &p) {
      return p.get() == bracket.node;
    });
    std::string text;
    if (op_it == nodes_.end())
      return text;
    for (auto it = op_it + 1; it != nodes_.end(); ++it)
      text += (*it)->literal;
    return text;
  };

  std::size_t save = pos_;
  std::optional<std::string> dest, title, label;
  bool resolved = false;
  bool tried_full_ref = false;

  // 1. Inline link: ](destination title?)
  if (!resolved && pos_ < input_.size() && input_[pos_] == '(') {
    std::size_t il_save = pos_;
    ++pos_;
    while (
        pos_ < input_.size() &&
        (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n'))
      ++pos_;
    auto d = scanLinkDestination();
    if (d) {
      dest = d;
      while (
          pos_ < input_.size() &&
          (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n'))
        ++pos_;
      if (pos_ < input_.size() &&
          (input_[pos_] == '"' || input_[pos_] == '\'' ||
           input_[pos_] == '(')) {
        auto t = scanLinkTitle();
        if (t) {
          title = t;
          while (pos_ < input_.size() &&
                 (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                  input_[pos_] == '\n'))
            ++pos_;
        }
      }
      if (pos_ < input_.size() && input_[pos_] == ')') {
        ++pos_;
        resolved = true;
      }
    }
    if (!resolved) {
      pos_ = il_save;
      dest = std::nullopt;
      title = std::nullopt;
    }
  }

  // 2. Full reference: ][label]
  if (!resolved && pos_ < input_.size() && input_[pos_] == '[') {
    tried_full_ref = true;
    std::size_t fr_save = pos_;
    ++pos_;
    std::size_t ls = pos_;
    bool label_ok = true;
    while (pos_ < input_.size() && pos_ - ls < 999) {
      char lc = input_[pos_];
      if (lc == ']')
        break;
      if (lc == '[' || lc == '\n') {
        label_ok = false;
        break;
      }
      if (lc == '\\' && pos_ + 1 < input_.size()) {
        pos_ += 2;
        continue;
      }
      ++pos_;
    }
    if (label_ok && pos_ < input_.size() && input_[pos_] == ']' && pos_ > ls) {
      std::string raw(input_.substr(ls, pos_ - ls));
      ++pos_;
      auto it = ref_map_->find(normaliseLabel(raw));
      if (it != ref_map_->end()) {
        dest = it->second.destination;
        title = it->second.title;
        label = raw;
        resolved = true;
      }
    }
    if (!resolved)
      pos_ = fr_save;
  }

  // Raw label text: input between the '[' and this ']' (pos_-1 after ++pos_).
  // Used for shortcut/collapsed lookups to avoid backslash-processing effects.
  auto raw_bracket_label = [&]() -> std::string {
    std::size_t end = save - 1; // save is pos_ after ']++'
    if (bracket.src_pos <= end)
      return std::string(input_.substr(bracket.src_pos, end - bracket.src_pos));
    return {};
  };

  // 3. Collapsed reference: ][]
  if (!resolved && pos_ + 1 < input_.size() && input_[pos_] == '[' &&
      input_[pos_ + 1] == ']') {
    std::string raw = raw_bracket_label();
    auto it = ref_map_->find(normaliseLabel(raw));
    if (it != ref_map_->end()) {
      pos_ += 2;
      dest = it->second.destination;
      title = it->second.title;
      label = raw;
      resolved = true;
    }
  }

  // 4. Shortcut reference: label = raw bracket text (no ][label] was tried)
  if (!resolved && !tried_full_ref) {
    std::string raw = raw_bracket_label();
    auto it = ref_map_->find(normaliseLabel(raw));
    if (it != ref_map_->end()) {
      dest = it->second.destination;
      title = it->second.title;
      label = raw;
      resolved = true;
    }
  }

  if (resolved) {
    processEmphasis(bracket.delim_top);

    auto link =
        makeNode(bracket.is_image ? InlineType::Image : InlineType::Link);
    link->data = LinkData{dest.value(), title, label};

    auto op_it = std::find_if(nodes_.begin(), nodes_.end(), [&](const auto &p) {
      return p.get() == bracket.node;
    });

    if (op_it != nodes_.end()) {
      for (auto it = op_it + 1; it != nodes_.end(); ++it)
        link->children.push_back(std::move(*it));
      nodes_.erase(op_it + 1, nodes_.end());
      nodes_.erase(op_it); // remove the "[" / "![" text node
    }

    // Links cannot be nested: deactivate any preceding link (not image)
    // openers.
    if (!bracket.is_image) {
      for (auto &b : brackets_)
        if (!b.is_image)
          b.node = nullptr;
    }

    brackets_.pop_back();
    return link;
  }

  // Failed: bracket opener becomes literal; delimiters inside remain active.
  pos_ = save;
  brackets_.pop_back();
  return literal_bracket();
}

// ── handleEmphasis (spec §6.4)
// ────────────────────────────────────────────────

void InlineParser::handleEmphasis(char delim_char, std::size_t run_len) {
  // pos_ is already past the run; run starts at pos_ - run_len.
  // Decode the actual Unicode codepoints adjacent to the run.
  uint32_t prev_cp = (pos_ > run_len) ? cpBefore(input_, pos_ - run_len) : 0;
  uint32_t next_cp = (pos_ < input_.size()) ? cpAt(input_, pos_) : 0;

  bool prev_ws = (prev_cp == 0) || isUnicodeWhitespaceCp(prev_cp);
  bool next_ws = (next_cp == 0) || isUnicodeWhitespaceCp(next_cp);
  bool prev_punct = isUnicodePunctCp(prev_cp);
  bool next_punct = isUnicodePunctCp(next_cp);

  bool left_flanking = !next_ws && (!next_punct || prev_ws || prev_punct);
  bool right_flanking = !prev_ws && (!prev_punct || next_ws || next_punct);

  bool can_open, can_close;
  if (delim_char == '*') {
    can_open = left_flanking;
    can_close = right_flanking;
  } else { // '_'
    can_open = left_flanking && (!right_flanking || prev_punct);
    can_close = right_flanking && (!left_flanking || next_punct);
  }

  if (!can_open && !can_close)
    return; // literal text, no delimiter pushed

  Delimiter d;
  d.ch = delim_char;
  d.num = static_cast<int>(run_len);
  d.orig_num = static_cast<int>(run_len);
  d.can_open = can_open;
  d.can_close = can_close;
  d.node = nullptr; // set by parseInline after this call returns
  delimiters_.push_back(d);
}

// ── processEmphasis (CommonMark appendix algorithm)
// ───────────────────────────

void InlineParser::processEmphasis(std::optional<std::size_t> stack_bottom) {
  std::size_t bot = stack_bottom.value_or(0);

  // openers_bottom[char_idx][n%3][can_also_open] per spec appendix A.
  // char_idx: 0='*', 1='_'
  std::size_t ob[2][3][2];
  for (int ci2 = 0; ci2 < 2; ++ci2)
    for (int m = 0; m < 3; ++m)
      for (int k = 0; k < 2; ++k)
        ob[ci2][m][k] = bot;

  auto oi = [](char c) -> int { return c == '*' ? 0 : 1; };

  std::size_t ci = bot; // closer index

  while (ci < delimiters_.size()) {
    Delimiter &closer = delimiters_[ci];
    if (!closer.can_close) {
      ++ci;
      continue;
    }

    char c = closer.ch;
    int cm3 = closer.orig_num % 3;
    int ckey = closer.can_open ? 1 : 0;
    std::size_t ob_floor = ob[oi(c)][cm3][ckey];

    // Search backward for a matching opener.
    bool found = false;
    std::size_t opIdx = ci;
    while (opIdx > ob_floor) {
      --opIdx;
      Delimiter &opener = delimiters_[opIdx];
      if (!opener.can_open || opener.ch != c)
        continue;
      // Sum rule (spec §6.2, appendix A): skip if EITHER can both open+close,
      // closer.orig % 3 != 0, AND (opener.orig + closer.orig) % 3 == 0.
      if ((opener.can_open && opener.can_close) ||
          (delimiters_[ci].can_open && delimiters_[ci].can_close)) {
        if (delimiters_[ci].orig_num % 3 != 0 &&
            (opener.orig_num + delimiters_[ci].orig_num) % 3 == 0)
          continue;
      }
      found = true;
      break;
    }

    if (!found) {
      ob[oi(c)][cm3][ckey] = ci; // update lower bound for this bucket only
      if (!delimiters_[ci].can_open)
        delimiters_.erase(delimiters_.begin() + ci);
      else
        ++ci;
      continue;
    }

    // Matched opener at opIdx, closer at ci.
    int use = (delimiters_[opIdx].num >= 2 && delimiters_[ci].num >= 2) ? 2 : 1;
    auto type = (use == 2) ? InlineType::Strong : InlineType::Emph;

    InlineNode *op_ptr = delimiters_[opIdx].node;
    InlineNode *cl_ptr = delimiters_[ci].node;

    auto op_it =
        std::find_if(nodes_.begin(), nodes_.end(),
                     [op_ptr](const auto &p) { return p.get() == op_ptr; });
    auto cl_it =
        std::find_if(nodes_.begin(), nodes_.end(),
                     [cl_ptr](const auto &p) { return p.get() == cl_ptr; });

    auto emph = makeNode(type);
    for (auto it = op_it + 1; it != cl_it; ++it)
      emph->children.push_back(std::move(*it));

    // Replace nodes (op_it+1 .. cl_it) with the emph node.
    auto ins = nodes_.erase(op_it + 1, cl_it);
    nodes_.insert(ins, std::move(emph));

    // Shrink delimiter counts; update literal strings.
    delimiters_[opIdx].num -= use;
    delimiters_[opIdx].node->literal =
        std::string(static_cast<std::size_t>(delimiters_[opIdx].num), c);

    delimiters_[ci].num -= use;
    delimiters_[ci].node->literal =
        std::string(static_cast<std::size_t>(delimiters_[ci].num), c);

    // Erase delimiter entries strictly between opener and closer.
    if (opIdx + 1 < ci) {
      delimiters_.erase(delimiters_.begin() + opIdx + 1,
                        delimiters_.begin() + ci);
      ci = opIdx + 1;
    }

    // Remove empty opener.
    if (delimiters_[opIdx].num == 0) {
      delimiters_.erase(delimiters_.begin() + opIdx);
      if (ci > opIdx)
        --ci;
    }

    // Remove empty closer (don't advance; next iteration retries this slot).
    if (ci < delimiters_.size() && delimiters_[ci].num == 0)
      delimiters_.erase(delimiters_.begin() + ci);
    // If closer still has chars, stay at ci and match again.
  }

  // Remove all delimiter entries at or above bot.
  if (bot < delimiters_.size())
    delimiters_.erase(delimiters_.begin() + bot, delimiters_.end());
}

// ── normaliseLabel (spec §4.7, ASCII-only case folding)
// ───────────────────────

std::string InlineParser::normaliseLabel(std::string_view label) {
  // Collapse whitespace, then apply full Unicode case folding.
  std::string collapsed;
  collapsed.reserve(label.size());
  bool space = true;
  for (char c : label) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!space)
        collapsed += ' ';
      space = true;
    } else {
      collapsed += c;
      space = false;
    }
  }
  if (!collapsed.empty() && collapsed.back() == ' ')
    collapsed.pop_back();
  return unicode_fold::foldString(collapsed);
}

// ── makeNode
// ──────────────────────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::makeNode(InlineType type) {
  auto node = std::make_unique<InlineNode>();
  node->type = type;
  return node;
}
