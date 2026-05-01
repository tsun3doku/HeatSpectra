#include "NodeRemeshParams.hpp"

#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "nodegraph/ui/widgets/NodePanelUtils.hpp"

RemeshNodeParams readRemeshNodeParams(const NodeGraphNode& node) {
    constexpr int iterations = 1;
    constexpr double minAngleDegrees = 30.0;
    constexpr double maxEdgeLength = 0.1;
    constexpr double stepSize = 0.25;

    RemeshNodeParams params{};
    params.iterations = NodePanelUtils::readIntParam(node, nodegraphparams::remesh::Iterations, iterations);
    params.minAngleDegrees = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MinAngleDegrees, minAngleDegrees);
    params.maxEdgeLength = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MaxEdgeLength, maxEdgeLength);
    params.stepSize = NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::StepSize, stepSize);
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
