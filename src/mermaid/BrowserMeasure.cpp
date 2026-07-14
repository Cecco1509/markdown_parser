// BrowserMeasure.cpp — Emscripten/WASM only.
//
// A TextMeasurer that measures labels with the browser's real font engine via
// an offscreen <canvas>. The heavy lifting (the per-label loop) runs in one JS
// function so the C++/JS boundary is crossed once per batch; emscripten::val
// marshals the string array in and the float array out.
//
// JS contract:  __mmMeasureLabels(labels: string[], font: string)
//                   -> Float32Array [w0, h0, w1, h1, ...]
//
// See docs/mermaid/rendering.md and the design discussion for the rationale.

#include "mermaid/TextMeasure.hpp"

#include <emscripten/em_js.h>
#include <emscripten/val.h>

#include <memory>
#include <string>
#include <vector>

namespace mermaid {
namespace {

// Install the batch measurer once on globalThis. Reuses a single canvas; uses
// the glyph bounding box for height, falling back to fontSize*1.2 when the
// (older) browser doesn't report actualBoundingBox metrics.
EM_JS(void, mm_install_measurer, (), {
  if (globalThis.__mmMeasureLabels) return;
  const canvas = (typeof OffscreenCanvas !== 'undefined')
                     ? new OffscreenCanvas(1, 1)
                     : document.createElement('canvas');
  const ctx = canvas.getContext('2d');
  globalThis.__mmMeasureLabels = function (labels, font) {
    ctx.font = font;
    const out = new Float32Array(labels.length * 2);
    for (let i = 0; i < labels.length; i++) {
      const m = ctx.measureText(labels[i]);
      const h = (m.actualBoundingBoxAscent + m.actualBoundingBoxDescent) ||
                (parseFloat(font) * 1.2);
      out[2 * i] = m.width;
      out[2 * i + 1] = h;
    }
    return out;
  };
});

class BrowserMeasurer : public TextMeasurer {
public:
  std::vector<LabelBox> measure(const std::vector<std::string> &labels,
                                const FontSpec &font) override {
    mm_install_measurer();

    // Build the JS string[] of labels (val marshals each std::string -> JS string).
    emscripten::val js_labels = emscripten::val::array();
    for (const std::string &s : labels)
      js_labels.call<void>("push", emscripten::val(s));

    // CSS font shorthand, e.g. "16px trebuchet ms, verdana, arial, sans-serif".
    const std::string font_str = std::to_string(font.size_px) + "px " + font.family;

    // One boundary crossing: call the batch measurer, get a Float32Array back.
    emscripten::val result = emscripten::val::global("__mmMeasureLabels")(
        js_labels, emscripten::val(font_str));

    // Marshal the flat float array [w0,h0,w1,h1,...] and reshape into LabelBoxes.
    std::vector<float> flat =
        emscripten::convertJSArrayToNumberVector<float>(result);
    std::vector<LabelBox> out;
    out.reserve(labels.size());
    for (size_t i = 0; i + 1 < flat.size(); i += 2)
      out.push_back({static_cast<double>(flat[i]), static_cast<double>(flat[i + 1])});
    return out;
  }
};

} // namespace

std::unique_ptr<TextMeasurer> make_browser_measurer() {
  return std::make_unique<BrowserMeasurer>();
}

} // namespace mermaid
