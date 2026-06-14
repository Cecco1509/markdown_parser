#include "markdown_parser/utils/entities.hpp"
#include <cctype>
#include <cstdint>
#include <cstring>

// ── Unicode codepoint → UTF-8 string ─────────────────────────────────────────

static std::string cpToUtf8(uint32_t cp) {
  // Null and surrogates → U+FFFD replacement character.
  if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
    cp = 0xFFFD;
  std::string s;
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return s;
}

// ── Entity tables
// ─────────────────────────────────────────────────────────────

// Multi-codepoint entities (pre-encoded UTF-8).
struct MultiEntry {
  const char *name;
  const char *utf8;
};
static const MultiEntry kMultiCP[] = {
    {"ngE", "\xE2\x89\xA7\xCC\xB8"}, // U+2267 U+0338  ≧̸
};
static constexpr int kMultiCPCount =
    static_cast<int>(sizeof(kMultiCP) / sizeof(kMultiCP[0]));

// Single-codepoint entities — sorted by name (strcmp / ASCII order).
struct Entry {
  const char *name;
  uint32_t cp;
};
static const Entry kEntities[] = {
    // clang-format off
    // uppercase-first entries
    {"AElig",                    0x00C6},
    {"AMP",                      0x0026},
    {"Aacute",                   0x00C1},
    {"Acirc",                    0x00C2},
    {"Agrave",                   0x00C0},
    {"Alpha",                    0x0391},
    {"Aring",                    0x00C5},
    {"Atilde",                   0x00C3},
    {"Auml",                     0x00C4},
    {"Beta",                     0x0392},
    {"COPY",                     0x00A9},
    {"Ccedil",                   0x00C7},
    {"Chi",                      0x03A7},
    {"ClockwiseContourIntegral", 0x2232},
    {"Dagger",                   0x2021},
    {"Dcaron",                   0x010E},
    {"Delta",                    0x0394},
    {"DifferentialD",            0x2146},
    {"ETH",                      0x00D0},
    {"Eacute",                   0x00C9},
    {"Ecirc",                    0x00CA},
    {"Egrave",                   0x00C8},
    {"Epsilon",                  0x0395},
    {"Eta",                      0x0397},
    {"Euml",                     0x00CB},
    {"GT",                       0x003E},
    {"Gamma",                    0x0393},
    {"HilbertSpace",             0x210B},
    {"Iacute",                   0x00CD},
    {"Icirc",                    0x00CE},
    {"Igrave",                   0x00CC},
    {"Iota",                     0x0399},
    {"Iuml",                     0x00CF},
    {"Kappa",                    0x039A},
    {"LT",                       0x003C},
    {"Lambda",                   0x039B},
    {"Mu",                       0x039C},
    {"Nu",                       0x039D},
    {"Oacute",                   0x00D3},
    {"Ocirc",                    0x00D4},
    {"OElig",                    0x0152},
    {"Ograve",                   0x00D2},
    {"Omega",                    0x03A9},
    {"Omicron",                  0x039F},
    {"Otilde",                   0x00D5},
    {"Ouml",                     0x00D6},
    {"Pi",                       0x03A0},
    {"Prime",                    0x2033},
    {"Psi",                      0x03A8},
    {"QUOT",                     0x0022},
    {"REG",                      0x00AE},
    {"Rho",                      0x03A1},
    {"Scaron",                   0x0160},
    {"Sigma",                    0x03A3},
    {"THORN",                    0x00DE},
    {"Tau",                      0x03A4},
    {"Theta",                    0x0398},
    {"Uacute",                   0x00DA},
    {"Ucirc",                    0x00DB},
    {"Ugrave",                   0x00D9},
    {"Upsilon",                  0x03A5},
    {"Uuml",                     0x00DC},
    {"Xi",                       0x039E},
    {"Yacute",                   0x00DD},
    {"Yuml",                     0x0178},
    {"Zeta",                     0x0396},
    // lowercase entries
    {"aacute",   0x00E1},
    {"acirc",    0x00E2},
    {"acute",    0x00B4},
    {"aelig",    0x00E6},
    {"agrave",   0x00E0},
    {"alefsym",  0x2135},
    {"alpha",    0x03B1},
    {"amp",      0x0026},
    {"and",      0x2227},
    {"ang",      0x2220},
    {"apos",     0x0027},
    {"aring",    0x00E5},
    {"asymp",    0x2248},
    {"atilde",   0x00E3},
    {"auml",     0x00E4},
    {"bdquo",    0x201E},
    {"beta",     0x03B2},
    {"brvbar",   0x00A6},
    {"bull",     0x2022},
    {"cap",      0x2229},
    {"ccedil",   0x00E7},
    {"cedil",    0x00B8},
    {"cent",     0x00A2},
    {"chi",      0x03C7},
    {"circ",     0x02C6},
    {"clubs",    0x2663},
    {"cong",     0x2245},
    {"copy",     0x00A9},
    {"crarr",    0x21B5},
    {"cup",      0x222A},
    {"curren",   0x00A4},
    {"dagger",   0x2020},
    {"darr",     0x2193},
    {"deg",      0x00B0},
    {"delta",    0x03B4},
    {"diams",    0x2666},
    {"divide",   0x00F7},
    {"eacute",   0x00E9},
    {"ecirc",    0x00EA},
    {"egrave",   0x00E8},
    {"emsp",     0x2003},
    {"empty",    0x2205},
    {"ensp",     0x2002},
    {"epsilon",  0x03B5},
    {"equiv",    0x2261},
    {"eta",      0x03B7},
    {"eth",      0x00F0},
    {"euml",     0x00EB},
    {"exist",    0x2203},
    {"fnof",     0x0192},
    {"forall",   0x2200},
    {"frac12",   0x00BD},
    {"frac14",   0x00BC},
    {"frac34",   0x00BE},
    {"frasl",    0x2044},
    {"gamma",    0x03B3},
    {"ge",       0x2265},
    {"gt",       0x003E},
    {"hArr",     0x21D4},
    {"harr",     0x2194},
    {"hearts",   0x2665},
    {"hellip",   0x2026},
    {"iacute",   0x00ED},
    {"icirc",    0x00EE},
    {"igrave",   0x00EC},
    {"image",    0x2111},
    {"infin",    0x221E},
    {"int",      0x222B},
    {"iota",     0x03B9},
    {"iquest",   0x00BF},
    {"iuml",     0x00EF},
    {"kappa",    0x03BA},
    {"lArr",     0x21D0},
    {"lang",     0x2329},
    {"laquo",    0x00AB},
    {"larr",     0x2190},
    {"lceil",    0x2308},
    {"ldquo",    0x201C},
    {"le",       0x2264},
    {"lfloor",   0x230A},
    {"lowast",   0x2217},
    {"loz",      0x25CA},
    {"lrm",      0x200E},
    {"lsaquo",   0x2039},
    {"lsquo",    0x2018},
    {"lt",       0x003C},
    {"macr",     0x00AF},
    {"mdash",    0x2014},
    {"micro",    0x00B5},
    {"middot",   0x00B7},
    {"minus",    0x2212},
    {"mu",       0x03BC},
    {"nabla",    0x2207},
    {"nbsp",     0x00A0},
    {"ndash",    0x2013},
    {"ne",       0x2260},
    {"ni",       0x220B},
    {"not",      0x00AC},
    {"notin",    0x2209},
    {"nsub",     0x2284},
    {"ntilde",   0x00F1},
    {"nu",       0x03BD},
    {"oacute",   0x00F3},
    {"ocirc",    0x00F4},
    {"oelig",    0x0153},
    {"ograve",   0x00F2},
    {"oline",    0x203E},
    {"omega",    0x03C9},
    {"omicron",  0x03BF},
    {"oplus",    0x2295},
    {"or",       0x2228},
    {"ordf",     0x00AA},
    {"ordm",     0x00BA},
    {"oslash",   0x00F8},
    {"otilde",   0x00F5},
    {"otimes",   0x2297},
    {"ouml",     0x00F6},
    {"para",     0x00B6},
    {"part",     0x2202},
    {"permil",   0x2030},
    {"phi",      0x03C6},
    {"pi",       0x03C0},
    {"piv",      0x03D6},
    {"plusmn",   0x00B1},
    {"pound",    0x00A3},
    {"prime",    0x2032},
    {"prod",     0x220F},
    {"prop",     0x221D},
    {"psi",      0x03C8},
    {"quot",     0x0022},
    {"rArr",     0x21D2},
    {"rang",     0x232A},
    {"raquo",    0x00BB},
    {"rarr",     0x2192},
    {"rceil",    0x2309},
    {"rdquo",    0x201D},
    {"real",     0x211C},
    {"reg",      0x00AE},
    {"rfloor",   0x230B},
    {"rho",      0x03C1},
    {"rlm",      0x200F},
    {"rsaquo",   0x203A},
    {"rsquo",    0x2019},
    {"sbquo",    0x201A},
    {"scaron",   0x0161},
    {"sdot",     0x22C5},
    {"sect",     0x00A7},
    {"shy",      0x00AD},
    {"sigma",    0x03C3},
    {"sigmaf",   0x03C2},
    {"sim",      0x223C},
    {"spades",   0x2660},
    {"sub",      0x2282},
    {"sube",     0x2286},
    {"sum",      0x2211},
    {"sup",      0x2283},
    {"sup1",     0x00B9},
    {"sup2",     0x00B2},
    {"sup3",     0x00B3},
    {"supe",     0x2287},
    {"szlig",    0x00DF},
    {"tau",      0x03C4},
    {"there4",   0x2234},
    {"theta",    0x03B8},
    {"thetasym", 0x03D1},
    {"thinsp",   0x2009},
    {"thorn",    0x00FE},
    {"tilde",    0x02DC},
    {"times",    0x00D7},
    {"trade",    0x2122},
    {"uArr",     0x21D1},
    {"uarr",     0x2191},
    {"uml",      0x00A8},
    {"upsih",    0x03D2},
    {"upsilon",  0x03C5},
    {"weierp",   0x2118},
    {"xi",       0x03BE},
    {"yen",      0x00A5},
    {"yuml",     0x00FF},
    {"zwnj",     0x200C},
    {"zwj",      0x200D},
    // clang-format on
};
static constexpr int kEntityCount =
    static_cast<int>(sizeof(kEntities) / sizeof(kEntities[0]));

static const char *lookupNamed(std::string_view name) {
  // Check multi-codepoint table first.
  for (int i = 0; i < kMultiCPCount; ++i) {
    if (name == kMultiCP[i].name)
      return kMultiCP[i].utf8;
  }
  // Binary search single-codepoint table.
  int lo = 0, hi = kEntityCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int cmp = std::strcmp(kEntities[mid].name,
                          std::string(name).c_str()); // need NUL-terminated
    if (cmp == 0)
      return nullptr; // found — signal via codepoint instead
    if (cmp < 0)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return nullptr;
}

static uint32_t lookupNamedCp(std::string_view name) {
  // Binary search single-codepoint table.
  int lo = 0, hi = kEntityCount - 1;
  std::string key(name);
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int cmp = std::strcmp(kEntities[mid].name, key.c_str());
    if (cmp == 0)
      return kEntities[mid].cp;
    if (cmp < 0)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

// ── Public API
// ────────────────────────────────────────────────────────────────

namespace markdown_parser {
namespace entities {

std::string decode(std::string_view input, std::size_t &pos) {
  if (pos >= input.size() || input[pos] != '&')
    return {};

  std::size_t i = pos + 1;
  if (i >= input.size())
    return {};

  if (input[i] == '#') {
    // Numeric character reference
    ++i;
    if (i >= input.size())
      return {};

    bool is_hex = false;
    if (input[i] == 'x' || input[i] == 'X') {
      is_hex = true;
      ++i;
    }

    std::size_t start = i;
    uint32_t cp = 0;
    // Spec limits: 1-6 hex digits, 1-7 decimal digits.
    const std::size_t max_digits = is_hex ? 6 : 7;
    if (is_hex) {
      while (i < input.size() && (i - start) < max_digits &&
             std::isxdigit(static_cast<unsigned char>(input[i]))) {
        char c = input[i];
        cp = cp * 16 +
             (std::isdigit(static_cast<unsigned char>(c))
                  ? (c - '0')
                  : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
        ++i;
      }
    } else {
      while (i < input.size() && (i - start) < max_digits &&
             std::isdigit(static_cast<unsigned char>(input[i]))) {
        cp = cp * 10 + (input[i] - '0');
        ++i;
      }
    }

    if (i == start || i >= input.size() || input[i] != ';')
      return {};
    ++i; // consume ';'
    pos = i;
    return cpToUtf8(cp);
  }

  // Named entity: & followed by name then ;
  std::size_t start = i;
  while (i < input.size() &&
         (std::isalnum(static_cast<unsigned char>(input[i])) ||
          input[i] == '_' ||
          input[i] == '-')) // some HTML5 names have -/_ but standard ones don't
    ++i;

  if (i == start || i >= input.size() || input[i] != ';')
    return {};

  std::string_view name = input.substr(start, i - start);

  // Check multi-codepoint table.
  for (int j = 0; j < kMultiCPCount; ++j) {
    if (name == kMultiCP[j].name) {
      pos = i + 1;
      return std::string(kMultiCP[j].utf8);
    }
  }

  // Binary search single-codepoint table.
  uint32_t cp = lookupNamedCp(name);
  if (cp == 0)
    return {};

  pos = i + 1; // consume ';'
  return cpToUtf8(cp);
}

std::string decodeAll(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  std::size_t pos = 0;
  while (pos < input.size()) {
    if (input[pos] == '&') {
      std::size_t save = pos;
      std::string decoded = decode(input, pos);
      if (!decoded.empty()) {
        out += decoded;
      } else {
        out += input[save];
        pos = save + 1;
      }
    } else {
      out += input[pos++];
    }
  }
  return out;
}

} // namespace entities
} // namespace markdown_parser
