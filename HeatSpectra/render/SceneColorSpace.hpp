#pragma once

#include <algorithm>
#include <array>
#include <cmath>

inline constexpr std::array<float, 4> clearColorLinear = {0.0144f, 0.0144f, 0.0168f, 1.0f};

inline float linearToSRGBChannel(float v) {
    const float c = std::max(v, 0.0f);
    if (c <= 0.0031308f)
        return 12.92f * c;
    return static_cast<float>(1.055 * std::pow(static_cast<double>(c), 1.0 / 2.4) - 0.055);
}

inline std::array<float, 4> linearToSRGB(const std::array<float, 4>& linear) {
    return {
        linearToSRGBChannel(linear[0]),
        linearToSRGBChannel(linear[1]),
        linearToSRGBChannel(linear[2]),
        std::clamp(linear[3], 0.0f, 1.0f)
    };
}

inline std::array<float, 4> clearColorSRGBA() {
    return linearToSRGB(clearColorLinear);
}
