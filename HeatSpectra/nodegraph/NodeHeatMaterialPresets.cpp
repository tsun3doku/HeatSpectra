#include "NodeHeatMaterialPresets.hpp"

#include "NodePanelUtils.hpp"

bool tryResolveHeatPresetId(const std::string& value, HeatMaterialPresetId& outId) {
    const std::string normalized = NodePanelUtils::normalizePresetName(value);
    if (normalized == "copper") {
        outId = HeatMaterialPresetId::Copper;
        return true;
    }
    if (normalized == "iron") {
        outId = HeatMaterialPresetId::Iron;
        return true;
    }
    if (normalized == "ceramic") {
        outId = HeatMaterialPresetId::Ceramic;
        return true;
    }
    if (normalized == "custom") {
        outId = HeatMaterialPresetId::Custom;
        return true;
    }
    if (normalized == "aluminum" || normalized == "aluminium") {
        outId = HeatMaterialPresetId::Aluminum;
        return true;
    }
    return false;
}
