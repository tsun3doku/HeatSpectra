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
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedPayloadType;
        outputValue.payloadHandle = {};
        if (!payloadRegistry) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        if (!inputMeshValue || inputMeshValue->payloadHandle.key == 0 ||
            valueTypeOf(inputMeshValue->dataType) != NodeGraphValueType::Mesh) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }
        const uint64_t meshPayloadHash = payloadRegistry->resolvePayloadHash(inputMeshValue->dataType, inputMeshValue->payloadHandle);
        HeatReceiverData payload{};
        payload.meshHandle = inputMeshValue->payloadHandle;
        payload.meshPayloadHash = meshPayloadHash;
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(payload));
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeHeatReceiver::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, inputMeshValue);
    return true;
}
