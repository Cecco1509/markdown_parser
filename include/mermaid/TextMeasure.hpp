#pragma once

#include <string>
#include <vector>

// Label measurement, abstracted so the layout engine does not depend on *how*
// labels are sized. Different builds inject different measurers (see
// docs/mermaid/rendering.md): the CLI uses ApproxMeasurer; the web build can add
// a BrowserMeasurer behind this same interface.

namespace mermaid {

struct FontSpec {
  std::string family = "trebuchet ms, verdana, arial, sans-serif";
  double size_px = 16.0;
};

struct LabelBox {
  double w = 0;
  double h = 0;
};

class TextMeasurer {
public:
  virtual ~TextMeasurer() = default;
  // Batch: measure every label in one call. Result is aligned to `labels`.
  virtual std::vector<LabelBox> measure(const std::vector<std::string> &labels,
                                        const FontSpec &font) = 0;
};

// Character-advance heuristic. Zero dependencies; approximate widths. Good
// enough for a first cut; better measurers plug in behind TextMeasurer.
class ApproxMeasurer : public TextMeasurer {
public:
  std::vector<LabelBox> measure(const std::vector<std::string> &labels,
                                const FontSpec &font) override;
};

#ifdef __EMSCRIPTEN__
#include <memory>
// Browser-backed measurer (accurate, uses the real font engine). Defined in
// BrowserMeasure.cpp, compiled only in the Emscripten target.
std::unique_ptr<TextMeasurer> make_browser_measurer();
#endif

} // namespace mermaid
