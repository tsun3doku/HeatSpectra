#include "NodeHeatMaterialPresets.hpp"

#include "NodeGraphParamUtils.hpp"
#include "NodeGraphUtils.hpp"
#include "nodegraph/ui/widgets/NodePanelUtils.hpp"

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

bool tryMakeMaterialBindingRow(
    const std::string& receiverText,
    const std::string& presetText,
    HeatMaterialBindingRow& outRow) {
    uint32_t receiverModelNodeId = 0;
    if (!NodePanelUtils::tryParseUint32Id(receiverText, receiverModelNodeId) || receiverModelNodeId == 0) {
        return false;
    }

    HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
    if (!tryResolveHeatPresetId(presetText, presetId)) {
        return false;
    }

    outRow.receiverModelNodeId = receiverModelNodeId;
    outRow.presetId = presetId;
    return true;
}

std::vector<HeatMaterialBindingRow> readMaterialBindingRows(const NodeGraphParamValue& value) {
    std::vector<HeatMaterialBindingRow> rows;
    if (value.type != NodeGraphParamType::Array) {
        return rows;
    }

    for (const NodeGraphParamValue& elementValue : value.arrayValues) {
        if (elementValue.type != NodeGraphParamType::Struct) {
            continue;
        }

        const NodeGraphParamValue* receiverModelField = findParamFieldValue(elementValue, "receiverModelNodeId");
        const NodeGraphParamValue* presetField = findParamFieldValue(elementValue, "preset");
        if (!receiverModelField || !presetField) {
            continue;
        }

        int64_t receiverModelNodeId = 0;
        if (!tryGetParamInt(*receiverModelField, receiverModelNodeId) || receiverModelNodeId <= 0) {
            continue;
        }
        std::string presetName;
        if (!tryGetParamEnum(*presetField, presetName)) {
            continue;
        }

        HeatMaterialBindingRow row{};
        if (!tryMakeMaterialBindingRow(std::to_string(receiverModelNodeId), presetName, row)) {
            continue;
        }
        rows.push_back(std::move(row));
    }

    return rows;
}

bool writeMaterialBindingRows(NodeGraphParamValue& value, const std::vector<HeatMaterialBindingRow>& rows) {
    if (value.type != NodeGraphParamType::Array) {
        return false;
    }

    value.arrayValues.clear();
    value.arrayValues.reserve(rows.size());
    for (const HeatMaterialBindingRow& row : rows) {
        if (!addArrayElement(
                value,
                makeStructParamValue({
                    makeParamFieldValue(
                        "receiverModelNodeId",
                        makeIntParamValue(static_cast<int64_t>(row.receiverModelNodeId))),
                    makeParamFieldValue(
                        "preset",
                        makeEnumParamValue(heatMaterialPresetName(row.presetId))),
                }))) {
            return false;
        }
    }

    return true;
}

HeatMaterialBinding makeHeatMaterialBinding(const HeatMaterialBindingRow& row) {
    HeatMaterialBinding binding{};
    binding.receiverModelNodeId = row.receiverModelNodeId;
    binding.presetId = row.presetId;
    return binding;
}

std::vector<HeatMaterialBinding> makeHeatMaterialBindings(const std::vector<HeatMaterialBindingRow>& rows) {
    std::vector<HeatMaterialBinding> bindings;
    bindings.reserve(rows.size());
    for (const HeatMaterialBindingRow& row : rows) {
        HeatMaterialBinding binding = makeHeatMaterialBinding(row);
        bindings.push_back(std::move(binding));
    }
    return bindings;
}
