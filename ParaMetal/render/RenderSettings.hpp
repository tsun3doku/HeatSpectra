#pragma once

#include <cstdint>

namespace app {

enum class WireframeMode {
    Off = 0,
    Wireframe = 1,
    Shaded = 2
};

enum class BackgroundMode : uint8_t {
    Image = 0,
    SolidColor = 1
};

struct RenderSettings {
    WireframeMode wireframeMode = WireframeMode::Off;
    bool gpuTimingOverlayEnabled = false;
    bool gridEnabled = false;
    BackgroundMode backgroundMode = BackgroundMode::Image;
    bool navigationCubeVisible = true;
    bool axisLabelsVisible = true;
};

} 
