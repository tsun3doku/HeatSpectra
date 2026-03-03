#pragma once

namespace app {

enum class WireframeMode {
    Off = 0,
    Wireframe = 1,
    Shaded = 2
};

struct RenderSettings {
    WireframeMode wireframeMode = WireframeMode::Off;
    bool intrinsicOverlayEnabled = false;
    bool heatOverlayEnabled = false;
    bool intrinsicNormalsEnabled = false;
    bool intrinsicVertexNormalsEnabled = false;
    bool surfelsEnabled = false;
    bool voronoiEnabled = false;
    bool pointsEnabled = false;
    bool contactLinesEnabled = false;
    bool gpuTimingOverlayEnabled = false;
    float intrinsicNormalLength = 0.05f;
};

} 
