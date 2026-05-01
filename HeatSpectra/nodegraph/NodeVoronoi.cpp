#include "NodeVoronoi.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodeVoronoiParams.hpp"

#include <unordered_set>

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
        if (valueTypeOf(inputValue.dataType) != NodeGraphValueType::Mesh ||
            inputValue.payloadHandle.key == 0) {
            continue;
        }

        const NodeDataHandle meshHandle = payloadRegistry
            ? payloadRegistry->resolveMeshHandle(inputValue.dataType, inputValue.payloadHandle)
            : NodeDataHandle{};
        if (meshHandle.key == 0) {
            continue;
        }

        if (seenMeshKeys.insert(meshHandle.key).second) {
            receiverMeshHandles.push_back(meshHandle);
            receiverPayloadHashes.push_back(payloadRegistry->resolvePayloadHash(inputValue.dataType, inputValue.payloadHandle));
        }
    }

    const VoronoiNodeParams nodeParams = readVoronoiNodeParams(context.node);

    const bool active = !receiverMeshHandles.empty();
    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != NodePayloadType::Voronoi) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        VoronoiData voronoiData{};
        voronoiData.cellSize = static_cast<float>(nodeParams.cellSize);
        voronoiData.voxelResolution = nodeParams.voxelResolution;
        voronoiData.receiverMeshHandles = receiverMeshHandles;
        voronoiData.receiverPayloadHashes = receiverPayloadHashes;
        voronoiData.active = active;
        const uint64_t payloadKey = makeSocketKey(context.node.id, context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(voronoiData));
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeVoronoi::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    for (const EvaluatedSocketValue* input : context.inputs) {
        const NodeDataBlock* inputData = nullptr;
        if (input && input->status == EvaluatedSocketStatus::Value) {
            if (valueTypeOf(input->data.dataType) == NodeGraphValueType::Mesh &&
                input->data.payloadHandle.key != 0) {
                inputData = &input->data;
            }
        }
        NodeGraphHash::combineInputHash(outHash, inputData);
    }

    const VoronoiNodeParams nodeParams = readVoronoiNodeParams(context.node);
    NodeGraphHash::combineFloat(outHash, static_cast<float>(nodeParams.cellSize));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(nodeParams.voxelResolution));
    return true;
}
