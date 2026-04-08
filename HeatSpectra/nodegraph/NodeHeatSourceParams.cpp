#include "NodeHeatSourceParams.hpp"

#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodePanelUtils.hpp"

HeatSourceNodeParams readHeatSourceNodeParams(const NodeGraphNode& node) {
    HeatSourceNodeParams params{};
    params.temperature = NodePanelUtils::readFloatParam(
        node,
        nodegraphparams::heatsource::Temperature,
        100.0);
    return params;
}

bool writeHeatSourceNodeParams(
    NodeGraphEditor& editor,
    NodeGraphNodeId nodeId,
    const HeatSourceNodeParams& params) {
    return editor.setNodeParameter(
        nodeId,
        NodeGraphParamValue{
            nodegraphparams::heatsource::Temperature,
            NodeGraphParamType::Float,
            params.temperature});
}
