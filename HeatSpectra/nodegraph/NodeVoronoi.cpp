#include "NodeVoronoi.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodePanelUtils.hpp"
#include "NodePayloadRegistry.hpp"
#include "domain/VoronoiParams.hpp"

#include <unordered_set>

const char* NodeVoronoi::typeId() const {
    return nodegraphtypes::Voronoi;
}

bool NodeVoronoi::execute(NodeGraphKernelContext& context) const {
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    std::vector<NodeDataHandle> receiverGeometryHandles;
    std::unordered_set<uint64_t> seenGeometryKeys;

    for (const NodeDataBlock* inputValue : context.inputs) {
        if (!inputValue || inputValue->dataType != NodeDataType::Geometry || !payloadRegistry) {
            continue;
        }

        const GeometryData* geometry = payloadRegistry->get<GeometryData>(inputValue->payloadHandle);
        if (!geometry || geometry->modelId == 0) {
            continue;
        }

        if (inputValue->payloadHandle.key != 0 && seenGeometryKeys.insert(inputValue->payloadHandle.key).second) {
            receiverGeometryHandles.push_back(inputValue->payloadHandle);
        }
    }

    VoronoiParams voronoiParams{};
    voronoiParams.cellSize = static_cast<float>(NodePanelUtils::readFloatParam(
        context.node,
        nodegraphparams::voronoi::CellSize,
        voronoiParams.cellSize));
    voronoiParams.voxelResolution = NodePanelUtils::readIntParam(
        context.node,
        nodegraphparams::voronoi::VoxelResolution,
        voronoiParams.voxelResolution);
    if (voronoiParams.cellSize <= 0.0f) {
        voronoiParams.cellSize = VoronoiParams{}.cellSize;
    }
    if (voronoiParams.voxelResolution <= 0) {
        voronoiParams.voxelResolution = VoronoiParams{}.voxelResolution;
    }

    const bool active = !receiverGeometryHandles.empty();
    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedDataType;

        if (!payloadRegistry || outputValue.dataType != NodeDataType::Voronoi) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        VoronoiData voronoiData{};
        voronoiData.params = voronoiParams;
        voronoiData.receiverGeometryHandles = receiverGeometryHandles;
        voronoiData.active = active;
        const uint64_t payloadKey = makeSocketKey(context.node.id, context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(voronoiData));
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}

bool NodeVoronoi::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    for (const NodeDataBlock* inputValue : context.inputs) {
        if (!inputValue || inputValue->dataType != NodeDataType::Geometry) {
            continue;
        }

        NodeGraphHash::combine(outHash, inputValue->payloadHandle.key);
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.revision);
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputValue->payloadHandle.count));
    }

    VoronoiParams voronoiParams{};
    voronoiParams.cellSize = static_cast<float>(NodePanelUtils::readFloatParam(
        context.node,
        nodegraphparams::voronoi::CellSize,
        voronoiParams.cellSize));
    voronoiParams.voxelResolution = NodePanelUtils::readIntParam(
        context.node,
        nodegraphparams::voronoi::VoxelResolution,
        voronoiParams.voxelResolution);
    if (voronoiParams.cellSize <= 0.0f) {
        voronoiParams.cellSize = VoronoiParams{}.cellSize;
    }
    if (voronoiParams.voxelResolution <= 0) {
        voronoiParams.voxelResolution = VoronoiParams{}.voxelResolution;
    }

    NodeGraphHash::combineFloat(outHash, voronoiParams.cellSize);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(voronoiParams.voxelResolution));
    return true;
}
