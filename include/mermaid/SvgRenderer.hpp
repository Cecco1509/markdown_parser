#pragma once

#include "mermaid/Layout.hpp"

#include <string>

// Renders a positioned Layout to an SVG string. Pure geometry -> SVG: it never
// computes positions, only draws, using mermaid's CSS class names + one template
// per ShapeKind + one marker per arrow type. See docs/mermaid/rendering.md.

namespace mermaid {

struct RenderOptions {
  FontSpec font;
  std::string svg_id = "mermaid-svg";
};

std::string render_svg(const Layout &layout, const RenderOptions &opts = {});

} // namespace mermaid
