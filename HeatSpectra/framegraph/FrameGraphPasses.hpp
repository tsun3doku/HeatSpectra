#pragma once

#include <string_view>

namespace framegraph::passes {

inline constexpr std::string_view Geometry = "GeometryPass";
inline constexpr std::string_view Lighting = "LightingPass";
inline constexpr std::string_view Overlay = "OverlayPass";
inline constexpr std::string_view Blend = "BlendPass";

} 
