#pragma once

#include <string_view>

namespace framegraph::resources {

inline constexpr std::string_view AlbedoMSAA = "AlbedoMSAA";
inline constexpr std::string_view NormalMSAA = "NormalMSAA";
inline constexpr std::string_view PositionMSAA = "PositionMSAA";
inline constexpr std::string_view DepthMSAA = "DepthMSAA";

inline constexpr std::string_view AlbedoResolve = "AlbedoResolve";
inline constexpr std::string_view NormalResolve = "NormalResolve";
inline constexpr std::string_view PositionResolve = "PositionResolve";
inline constexpr std::string_view DepthResolve = "DepthResolve";

inline constexpr std::string_view LightingMSAA = "LightingMSAA";
inline constexpr std::string_view LightingResolve = "LightingResolve";
inline constexpr std::string_view LineMSAA = "LineMSAA";
inline constexpr std::string_view LineResolve = "LineResolve";
inline constexpr std::string_view SurfaceMSAA = "SurfaceMSAA";
inline constexpr std::string_view SurfaceResolve = "SurfaceResolve";
inline constexpr std::string_view Swapchain = "Swapchain";

}
