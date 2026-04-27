#include "NodeRemeshParams.hpp"

#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "nodegraph/ui/widgets/NodePanelUtils.hpp"
#include "domain/RemeshParams.hpp"

RemeshNodeParams readRemeshNodeParams(const NodeGraphNode& node) {
    const RemeshParams defaults{};

    RemeshNodeParams params{};
    params.iterations = NodePanelUtils::readIntParam(node, nodegraphparams::remesh::Iterations, defaults.iterations);
    params.minAngleDegrees = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MinAngleDegrees, defaults.minAngleDegrees);
    params.maxEdgeLength = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MaxEdgeLength, defaults.maxEdgeLength);
    params.stepSize = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::StepSize, defaults.stepSize);
    params.preview.showRemeshOverlay = NodePanelUtils::readBoolParam(node, nodegraphparams::remesh::ShowRemeshOverlay, false);
    params.preview.showFaceNormals = NodePanelUtils::readBoolParam(node, nodegraphparams::remesh::ShowFaceNormals, false);
    params.preview.showVertexNormals = NodePanelUtils::readBoolParam(node, nodegraphparams::remesh::ShowVertexNormals, false);
    params.normalLength = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::NormalLength, 0.05);
    return params;
}

bool writeRemeshNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const RemeshNodeParams& params) {
    return editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::Iterations, NodeGraphParamType::Int, 0.0, params.iterations}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::MinAngleDegrees, NodeGraphParamType::Float, params.minAngleDegrees}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::MaxEdgeLength, NodeGraphParamType::Float, params.maxEdgeLength}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::StepSize, NodeGraphParamType::Float, params.stepSize}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::ShowRemeshOverlay, NodeGraphParamType::Bool, 0.0, 0, params.preview.showRemeshOverlay}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::ShowFaceNormals, NodeGraphParamType::Bool, 0.0, 0, params.preview.showFaceNormals}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::ShowVertexNormals, NodeGraphParamType::Bool, 0.0, 0, params.preview.showVertexNormals}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::remesh::NormalLength, NodeGraphParamType::Float, params.normalLength});
}
