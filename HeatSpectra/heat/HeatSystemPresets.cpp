#include "HeatSystemPresets.hpp"

namespace {

HeatMaterialPreset aluminumPreset = {
    HeatMaterialPresetId::Aluminum,
    "Aluminum",
    2700.0f,
    900.0f,
    205.0f,
};

HeatMaterialPreset copperPreset = {
    HeatMaterialPresetId::Copper,
    "Copper",
    8960.0f,
    385.0f,
    401.0f,
};

HeatMaterialPreset ironPreset = {
    HeatMaterialPresetId::Iron,
    "Iron",
    7874.0f,
    449.0f,
    80.4f,
};

HeatMaterialPreset ceramicPreset = {
    HeatMaterialPresetId::Ceramic,
    "Ceramic",
    2400.0f,
    800.0f,
    1.5f,
};

} // namespace

const HeatMaterialPreset& heatMaterialPresetById(HeatMaterialPresetId id) {
    switch (id) {
    case HeatMaterialPresetId::Copper:
        return copperPreset;
    case HeatMaterialPresetId::Iron:
        return ironPreset;
    case HeatMaterialPresetId::Ceramic:
        return ceramicPreset;
    case HeatMaterialPresetId::Aluminum:
    default:
        return aluminumPreset;
    }
}

const char* heatMaterialPresetName(HeatMaterialPresetId id) {
    return heatMaterialPresetById(id).name;
}
