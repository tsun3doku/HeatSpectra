#include "NodeModelParams.hpp"

#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodePanelUtils.hpp"

ModelNodeParams readModelNodeParams(const NodeGraphNode& node) {
    ModelNodeParams params{};
    params.path = NodePanelUtils::readStringParam(node, nodegraphparams::model::Path);
    return params;
}

bool writeModelNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const ModelNodeParams& params) {
    return editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::model::Path, NodeGraphParamType::String, 0.0, 0, false, params.path});
}
