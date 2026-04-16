#pragma once

#include <cstdint>
#include <string>

enum class HeatMaterialPresetId : uint8_t {
    Aluminum = 0,
    Copper = 1,
    Iron = 2,
    Ceramic = 3,
    Custom = 4,
};

struct HeatMaterialPreset {
    HeatMaterialPresetId id = HeatMaterialPresetId::Aluminum;
    const char* name = "Aluminum";
    float density = 2700.0f;
    float specificHeat = 900.0f;
    float conductivity = 205.0f;
};

struct HeatMaterialBindingGroup {
    uint32_t id = 0;
    std::string name;
};

const HeatMaterialPreset& heatMaterialPresetById(HeatMaterialPresetId id);
const char* heatMaterialPresetName(HeatMaterialPresetId id);
