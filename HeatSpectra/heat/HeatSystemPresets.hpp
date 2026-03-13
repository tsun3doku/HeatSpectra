#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

struct HeatMaterialBindingEntry {
    std::string groupName;
    HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
};

struct HeatModelMaterialBindings {
    uint32_t runtimeModelId = 0;
    std::vector<uint32_t> triangleGroupIds;
    std::vector<HeatMaterialBindingGroup> groups;
    std::vector<HeatMaterialBindingEntry> bindings;
};

const HeatMaterialPreset& heatMaterialPresetById(HeatMaterialPresetId id);
const char* heatMaterialPresetName(HeatMaterialPresetId id);
