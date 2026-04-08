#include "NodeRemesh.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphHash.hpp"
#include "NodeRemeshParams.hpp"
#include "domain/RemeshParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

const char* NodeRemesh::typeId() const {
    return nodegraphtypes::Remesh;
}

bool NodeRemesh::execute(NodeGraphKernelContext& context) const {
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    RemeshData remeshData{};

    const NodeGraphSocket* meshInputSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const EvaluatedSocketValue* inputMesh =
        meshInputSocket ? readEvaluatedInput(context.node, meshInputSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* upstreamGeometryValue = readInputValue(inputMesh);
    if (upstreamGeometryValue && upstreamGeometryValue->dataType != NodePayloadType::Geometry) {
        upstreamGeometryValue = nullptr;
    }

    bool executed = false;
    if (payloadRegistry && upstreamGeometryValue && upstreamGeometryValue->payloadHandle.key != 0) {
        const uint64_t sourcePayloadHash = payloadHashForDataBlock(*upstreamGeometryValue, payloadRegistry);
        const RemeshNodeParams params = readRemeshNodeParams(context.node);
        remeshData.sourceMeshHandle = upstreamGeometryValue->payloadHandle;
        remeshData.params.iterations = params.iterations;
        remeshData.params.minAngleDegrees = static_cast<float>(params.minAngleDegrees);
        remeshData.params.maxEdgeLength = static_cast<float>(params.maxEdgeLength);
        remeshData.params.stepSize = static_cast<float>(params.stepSize);
        remeshData.active = true;
        remeshData.payloadHash = NodeGraphHash::start();
        NodeGraphHash::combine(remeshData.payloadHash, static_cast<uint64_t>(remeshData.active ? 1u : 0u));
        NodeGraphHash::combine(remeshData.payloadHash, sourcePayloadHash);
        NodeGraphHash::combine(remeshData.payloadHash, static_cast<uint64_t>(remeshData.params.iterations));
        NodeGraphHash::combineFloat(remeshData.payloadHash, remeshData.params.minAngleDegrees);
        NodeGraphHash::combineFloat(remeshData.payloadHash, remeshData.params.maxEdgeLength);
        NodeGraphHash::combineFloat(remeshData.payloadHash, remeshData.params.stepSize);

        const uint64_t outputSocketKey = makeSocketKey(context.node.id, context.node.outputs.front().id);
        for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
            NodeDataBlock& outputValue = context.outputs[outputIndex];
            outputValue.dataType = NodePayloadType::Remesh;
            outputValue.payloadHandle = payloadRegistry->upsert(outputSocketKey, remeshData);
            updateDataBlockMetadata(outputValue, payloadRegistry);
        }
        executed = true;
    }

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        if (!executed) {
            outputValue.dataType = NodePayloadType::Remesh;
            outputValue.payloadHandle = {};
            updateDataBlockMetadata(outputValue, payloadRegistry);
        }
    }

    return executed;
}

bool NodeRemesh::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const RemeshNodeParams params = readRemeshNodeParams(context.node);
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(params.iterations));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.minAngleDegrees));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.maxEdgeLength));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.stepSize));

    const NodeGraphSocket* meshInputSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const EvaluatedSocketValue* inputValueState =
        meshInputSocket ? readEvaluatedInput(context.node, meshInputSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputValue = readInputValue(inputValueState);
    if (!inputValue) {
        NodeGraphHash::combine(outHash, 0);
    } else {
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputValue->dataType));
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.key);
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.revision);
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.count);
    }
    return true;
}
