#include "NodeVoronoiParams.hpp"

#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "nodegraph/ui/widgets/NodePanelUtils.hpp"

VoronoiNodeParams readVoronoiNodeParams(const NodeGraphNode& node) {
    constexpr double cellSize = 0.005;
    constexpr int voxelResolution = 128;

    VoronoiNodeParams params{};
    params.cellSize = NodePanelUtils::readFloatParam(node, nodegraphparams::voronoi::CellSize, cellSize);
    params.voxelResolution = NodePanelUtils::readIntParam(node, nodegraphparams::voronoi::VoxelResolution, voxelResolution);
    if (params.cellSize <= 0.0) { params.cellSize = cellSize; }
    if (params.voxelResolution <= 0) { params.voxelResolution = voxelResolution; }
    params.preview.showVoronoi = NodePanelUtils::readBoolParam(node, nodegraphparams::voronoi::ShowVoronoi, false);
    params.preview.showPoints = NodePanelUtils::readBoolParam(node, nodegraphparams::voronoi::ShowPoints, false);
    return params;
}

bool writeVoronoiNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const VoronoiNodeParams& params) {
    return editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::voronoi::CellSize, NodeGraphParamType::Float, params.cellSize}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::voronoi::VoxelResolution, NodeGraphParamType::Int, 0.0, params.voxelResolution}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::voronoi::ShowVoronoi, NodeGraphParamType::Bool, 0.0, 0, params.preview.showVoronoi}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::voronoi::ShowPoints, NodeGraphParamType::Bool, 0.0, 0, params.preview.showPoints});
}
