#pragma once

#include "NodeGraphTypes.hpp"
#include <string>
#include <vector>

#include "heat/HeatSystemPresets.hpp"

struct HeatMaterialBindingRow {
    uint32_t receiverModelNodeId = 0;
    HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
};

bool tryResolveHeatPresetId(const std::string& value, HeatMaterialPresetId& outId);
bool tryMakeMaterialBindingRow(
    const std::string& receiverText,
    const std::string& presetText,
    HeatMaterialBindingRow& outRow);
std::vector<HeatMaterialBindingRow> readMaterialBindingRows(const NodeGraphParamValue& value);
bool writeMaterialBindingRows(NodeGraphParamValue& value, const std::vector<HeatMaterialBindingRow>& rows);
std::vector<HeatMaterialBindingEntry> readMaterialBindings(const NodeGraphParamValue& value);
