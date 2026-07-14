#include "mermaid/TextMeasure.hpp"

namespace mermaid {

// Count Unicode code points (not bytes) so multibyte labels aren't over-sized:
// a UTF-8 continuation byte has the top bits 10xxxxxx.
static size_t codepoints(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

std::vector<LabelBox> ApproxMeasurer::measure(const std::vector<std::string> &labels,
                                              const FontSpec &font) {
  // Rough average glyph advance for a proportional sans-serif font, plus a
  // single-line height. These constants only need to be in the right ballpark;
  // real fidelity comes from a BrowserMeasurer/metrics table later.
  const double char_w = 0.60 * font.size_px;
  const double line_h = 1.20 * font.size_px;

  std::vector<LabelBox> out;
  out.reserve(labels.size());
  for (const std::string &s : labels)
    out.push_back({static_cast<double>(codepoints(s)) * char_w, line_h});
  return out;
}

} // namespace mermaid
