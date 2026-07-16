#pragma once

#include "mermaid/Layout.hpp"

#include <string>

// Renders a positioned Layout to an SVG string. Pure geometry -> SVG: it never
// computes positions, only draws, using mermaid's CSS class names + one template
// per ShapeKind + one marker per arrow type. See docs/mermaid/rendering.md.

namespace mermaid
{

  struct RenderOptions
  {
    FontSpec font;
    std::string svg_id = "mermaid-svg";
    // Corner rounding where an edge bends. Clamped to half the shorter adjacent
    // segment, so short segments degrade gracefully instead of overshooting.
    double edge_corner_radius = 50;
  };

  std::string render_svg(const Layout &layout, const RenderOptions &opts = {});

} // namespace mermaid
