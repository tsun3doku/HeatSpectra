#include "NodeRemesh.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphHash.hpp"
#include "NodeRemeshParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

const char* NodeRemesh::typeId() const {
    return nodegraphtypes::Remesh;
}

void NodeRemesh::execute(NodeGraphKernelContext& context) const {
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    RemeshData remeshData{};

    const NodeGraphSocket* meshInputSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const EvaluatedSocketValue* inputMesh =
        meshInputSocket ? readEvaluatedInput(context.node, meshInputSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* upstreamGeometryValue = readInputValue(inputMesh);
    if (upstreamGeometryValue &&
        upstreamGeometryValue->dataType != NodePayloadType::Geometry &&
        upstreamGeometryValue->dataType != NodePayloadType::Remesh) {
        upstreamGeometryValue = nullptr;
    }

    const bool hasValidInput = payloadRegistry && upstreamGeometryValue && upstreamGeometryValue->payloadHandle.key != 0;
    if (hasValidInput) {
        const uint64_t sourcePayloadHash = payloadRegistry->resolvePayloadHash(upstreamGeometryValue->dataType, upstreamGeometryValue->payloadHandle);
        const RemeshNodeParams params = readRemeshNodeParams(context.node);
        remeshData.sourceMeshHandle = upstreamGeometryValue->payloadHandle;
        remeshData.sourcePayloadHash = sourcePayloadHash;
        remeshData.iterations = params.iterations;
        remeshData.minAngleDegrees = static_cast<float>(params.minAngleDegrees);
        remeshData.maxEdgeLength = static_cast<float>(params.maxEdgeLength);
        remeshData.stepSize = static_cast<float>(params.stepSize);
        remeshData.active = true;
    }

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != NodePayloadType::Remesh || !hasValidInput) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, remeshData);
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeRemesh::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* meshInputSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const EvaluatedSocketValue* inputValueState =
        meshInputSocket ? readEvaluatedInput(context.node, meshInputSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputValue = readInputValue(inputValueState);
    if (inputValue &&
        inputValue->dataType != NodePayloadType::Geometry &&
        inputValue->dataType != NodePayloadType::Remesh) {
        inputValue = nullptr;
    }

    const RemeshNodeParams params = readRemeshNodeParams(context.node);
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, inputValue);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(params.iterations));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.minAngleDegrees));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.maxEdgeLength));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.stepSize));
    return true;
}
