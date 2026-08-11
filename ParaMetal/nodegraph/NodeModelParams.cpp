#include "NodeModelParams.hpp"

#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "nodegraph/NodeParamUtils.hpp"

ModelNodeParams readModelNodeParams(const NodeGraphNode& node) {
    ModelNodeParams params{};
    params.path = NodeParamUtils::readStringParam(node, nodegraphparams::model::Path);
    if (const NodeGraphParamValue* value = findNodeParamValue(node, nodegraphparams::model::SourceUnit)) {
        units::tryParse(value->enumValue, params.sourceUnit);
    }
    return params;
}

bool writeModelNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const ModelNodeParams& params) {
    NodeGraphParamValue unit{};
    unit.id = nodegraphparams::model::SourceUnit;
    unit.type = NodeGraphParamType::Enum;
    unit.enumValue = std::string(units::displayName(params.sourceUnit));
    return editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::model::Path, NodeGraphParamType::String, 0.0, 0, false, params.path}) &&
           editor.setNodeParameter(nodeId, unit);
}
