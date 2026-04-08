#include "NodeVoronoi.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodeVoronoiParams.hpp"
#include "domain/VoronoiParams.hpp"

#include <unordered_set>

namespace {

VoronoiParams makeVoronoiParams(const VoronoiNodeParams& nodeParams) {
    const VoronoiParams defaults{};

    VoronoiParams params{};
    params.cellSize = static_cast<float>(nodeParams.cellSize);
    params.voxelResolution = nodeParams.voxelResolution;
    if (params.cellSize <= 0.0f) {
        params.cellSize = defaults.cellSize;
    }
    if (params.voxelResolution <= 0) {
        params.voxelResolution = defaults.voxelResolution;
    }
    return params;
}

}

const char* NodeVoronoi::typeId() const {
    return nodegraphtypes::Voronoi;
}

bool NodeVoronoi::execute(NodeGraphKernelContext& context) const {
    std::vector<NodeDataHandle> receiverMeshHandles;
    std::vector<uint64_t> receiverPayloadHashes;
    std::unordered_set<uint64_t> seenMeshKeys;
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    for (const EvaluatedSocketValue* input : context.inputs) {
        if (!input || input->status != EvaluatedSocketStatus::Value) {
            continue;
        }

        const NodeDataBlock& inputValue = input->data;
        if (inputValue.payloadHandle.key == 0) {
            continue;
        }

        if (seenMeshKeys.insert(inputValue.payloadHandle.key).second) {
            receiverMeshHandles.push_back(inputValue.payloadHandle);
            receiverPayloadHashes.push_back(payloadHashForDataBlock(inputValue, payloadRegistry));
        }
    }

    const VoronoiParams voronoiParams = makeVoronoiParams(readVoronoiNodeParams(context.node));

    const bool active = !receiverMeshHandles.empty();
    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != NodePayloadType::Voronoi) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        VoronoiData voronoiData{};
        voronoiData.params = voronoiParams;
        voronoiData.receiverMeshHandles = receiverMeshHandles;
        voronoiData.active = active;
        voronoiData.payloadHash = NodeGraphHash::start();
        NodeGraphHash::combine(voronoiData.payloadHash, static_cast<uint64_t>(voronoiData.active ? 1u : 0u));
        NodeGraphHash::combineFloat(voronoiData.payloadHash, voronoiData.params.cellSize);
        NodeGraphHash::combine(voronoiData.payloadHash, static_cast<uint64_t>(voronoiData.params.voxelResolution));
        NodeGraphHash::combine(voronoiData.payloadHash, static_cast<uint64_t>(receiverPayloadHashes.size()));
        for (uint64_t receiverPayloadHash : receiverPayloadHashes) {
            NodeGraphHash::combine(voronoiData.payloadHash, receiverPayloadHash);
        }
        const uint64_t payloadKey = makeSocketKey(context.node.id, context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(voronoiData));
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}

bool NodeVoronoi::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    for (const EvaluatedSocketValue* input : context.inputs) {
        if (!input || input->status != EvaluatedSocketStatus::Value) {
            continue;
        }

        const NodeDataBlock& inputValue = input->data;
        NodeGraphHash::combine(outHash, inputValue.payloadHandle.key);
        NodeGraphHash::combine(outHash, inputValue.payloadHandle.revision);
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputValue.payloadHandle.count));
    }

    const VoronoiParams voronoiParams = makeVoronoiParams(readVoronoiNodeParams(context.node));

    NodeGraphHash::combineFloat(outHash, voronoiParams.cellSize);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(voronoiParams.voxelResolution));
    return true;
}
