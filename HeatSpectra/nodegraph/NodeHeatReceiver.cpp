#include "NodeHeatReceiver.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphHash.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodePayloadRegistry.hpp"

const char* NodeHeatReceiver::typeId() const {
    return nodegraphtypes::HeatReceiver;
}

void NodeHeatReceiver::execute(NodeGraphKernelContext& context) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    NodeDataHandle meshHandle{};
    uint64_t meshPayloadHash = 0;
    const bool hasValidInput = payloadRegistry && inputMeshValue &&
        inputMeshValue->payloadHandle.key != 0 &&
        valueTypeOf(inputMeshValue->dataType) == NodeGraphValueType::Mesh;
    if (hasValidInput) {
        meshPayloadHash = payloadRegistry->resolvePayloadHash(inputMeshValue->dataType, inputMeshValue->payloadHandle);
        meshHandle = payloadRegistry->resolveMeshHandle(inputMeshValue->dataType, inputMeshValue->payloadHandle);
    }

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != NodePayloadType::HeatReceiver ||
            !hasValidInput || meshHandle.key == 0) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        HeatReceiverData payload{};
        payload.meshHandle = meshHandle;
        payload.meshPayloadHash = meshPayloadHash;
        const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(payload));
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeHeatReceiver::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    if (inputMeshValue && valueTypeOf(inputMeshValue->dataType) != NodeGraphValueType::Mesh) {
        inputMeshValue = nullptr;
    }

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, inputMeshValue);
    return true;
}
