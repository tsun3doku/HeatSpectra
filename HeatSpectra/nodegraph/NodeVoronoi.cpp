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

void NodeVoronoi::execute(NodeGraphKernelContext& context) const {
    // Voronoi is a variadic-input node: it accepts any number of connected mesh inputs.
    // context.inputs is iterated in socket order (guaranteed by the evaluator).
    // Single-input nodes use findInputSocket instead; Voronoi iterates all inputs.
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
            receiverPayloadHashes.push_back(payloadRegistry->resolvePayloadHash(inputValue.dataType, inputValue.payloadHandle));
        }
    }

    const VoronoiParams voronoiParams = makeVoronoiParams(readVoronoiNodeParams(context.node));

    const bool active = !receiverMeshHandles.empty();
    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedPayloadType;
        outputValue.payloadHandle = {};

        if (!payloadRegistry || outputValue.dataType != NodePayloadType::Voronoi) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        VoronoiData voronoiData{};
        voronoiData.params = voronoiParams;
        voronoiData.receiverMeshHandles = receiverMeshHandles;
        voronoiData.receiverPayloadHashes = receiverPayloadHashes;
        voronoiData.active = active;
        const uint64_t payloadKey = makeSocketKey(context.node.id, context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(voronoiData));
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeVoronoi::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    // Variadic-input hash: iterates context.inputs in socket order (guaranteed by evaluator).
    // Unconnected sockets hash as 0 to preserve position identity across different topologies.
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    for (const EvaluatedSocketValue* input : context.inputs) {
        const NodeDataBlock* inputData =
            (input && input->status == EvaluatedSocketStatus::Value) ? &input->data : nullptr;
        NodeGraphHash::combineInputHash(outHash, inputData);
    }

    const VoronoiParams voronoiParams = makeVoronoiParams(readVoronoiNodeParams(context.node));
    NodeGraphHash::combineFloat(outHash, voronoiParams.cellSize);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(voronoiParams.voxelResolution));
    return true;
}
