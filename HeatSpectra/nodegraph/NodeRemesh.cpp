#include "NodeRemesh.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphHash.hpp"
#include "NodePanelUtils.hpp"
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
        const RemeshParams defaults{};
        const uint64_t sourcePayloadHash = payloadHashForDataBlock(*upstreamGeometryValue, payloadRegistry);
        remeshData.sourceMeshHandle = upstreamGeometryValue->payloadHandle;
        remeshData.params.iterations = NodePanelUtils::readIntParam(
            context.node,
            nodegraphparams::remesh::Iterations,
            defaults.iterations);
        remeshData.params.minAngleDegrees = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node,
            nodegraphparams::remesh::MinAngleDegrees,
            defaults.minAngleDegrees));
        remeshData.params.maxEdgeLength = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node,
            nodegraphparams::remesh::MaxEdgeLength,
            defaults.maxEdgeLength));
        remeshData.params.stepSize = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node,
            nodegraphparams::remesh::StepSize,
            defaults.stepSize));
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
    const RemeshParams defaults{};
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(NodePanelUtils::readIntParam(
        context.node, nodegraphparams::remesh::Iterations, defaults.iterations)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::remesh::MinAngleDegrees, defaults.minAngleDegrees)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::remesh::MaxEdgeLength, defaults.maxEdgeLength)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::remesh::StepSize, defaults.stepSize)));

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
