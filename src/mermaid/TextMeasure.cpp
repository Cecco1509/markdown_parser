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
  // Calibrated against mermaid's own measurements (see tests/test-files/mermaid
  // goldens): its labels average ~0.5em per glyph, and its line box is 1.5em
  // (exactly 24px at the default 16px font). The old 0.6em/1.2em over-estimated
  // width — which inflates shapes derived from it, like the rhombus — while
  // under-estimating height, which squashed every box.
  const double char_w = 0.50 * font.size_px;
  const double line_h = 1.50 * font.size_px;

  std::vector<LabelBox> out;
  out.reserve(labels.size());
  for (const std::string &s : labels)
    out.push_back({static_cast<double>(codepoints(s)) * char_w, line_h});
  return out;
}

} // namespace mermaid
