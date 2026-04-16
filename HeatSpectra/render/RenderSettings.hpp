#pragma once

namespace app {

enum class WireframeMode {
    Off = 0,
    Wireframe = 1,
    Shaded = 2
};

struct RenderSettings {
    WireframeMode wireframeMode = WireframeMode::Off;
    bool gpuTimingOverlayEnabled = false;
};

} 
