#pragma once

#include <cstdint>

struct RuntimeThermalMaterial {
    uint32_t runtimeModelId = 0;
    float density = 2700.0f;
    float specificHeat = 900.0f;
    float conductivity = 205.0f;
};
