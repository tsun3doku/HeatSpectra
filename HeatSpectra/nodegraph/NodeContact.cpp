#include "NodeContact.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphHash.hpp"
#include "NodeContactParams.hpp"
#include "nodegraph/NodeGraphPayloadTypes.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

#include <cstdint>

const char* NodeContact::typeId() const {
    return nodegraphtypes::Contact;
}

void NodeContact::execute(NodeGraphKernelContext& context) const {
    const NodeGraphSocket* emitterSocket = findInputSocket(context.node, "Emitter");
    const NodeGraphSocket* receiverSocket = findInputSocket(context.node, "Receiver");
    const EvaluatedSocketValue* emitterInputValue =
        emitterSocket ? readEvaluatedInput(context.node, emitterSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* emitterInput = readInputValue(emitterInputValue);
    const EvaluatedSocketValue* receiverInputValue =
        receiverSocket ? readEvaluatedInput(context.node, receiverSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* receiverInput = readInputValue(receiverInputValue);

    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    NodeDataHandle emitterMeshHandle{};
    uint64_t emitterPayloadHash = 0;
    bool hasEmitterEndpoint = false;
    if (payloadRegistry && emitterInput &&
        emitterInput->payloadHandle.key != 0 &&
        valueTypeOf(emitterInput->dataType) == NodeGraphValueType::Mesh) {
        emitterPayloadHash = payloadRegistry->resolvePayloadHash(emitterInput->dataType, emitterInput->payloadHandle);
        emitterMeshHandle = payloadRegistry->resolveMeshHandle(emitterInput->dataType, emitterInput->payloadHandle);
        hasEmitterEndpoint = emitterMeshHandle.key != 0;
    }

    NodeDataHandle receiverMeshHandle{};
    uint64_t receiverPayloadHash = 0;
    bool hasReceiverEndpoint = false;
    if (payloadRegistry && receiverInput &&
        receiverInput->payloadHandle.key != 0 &&
        valueTypeOf(receiverInput->dataType) == NodeGraphValueType::Mesh) {
        receiverPayloadHash = payloadRegistry->resolvePayloadHash(receiverInput->dataType, receiverInput->payloadHandle);
        receiverMeshHandle = payloadRegistry->resolveMeshHandle(receiverInput->dataType, receiverInput->payloadHandle);
        hasReceiverEndpoint = receiverMeshHandle.key != 0;
    }

    const bool hasValidContact = hasEmitterEndpoint && hasReceiverEndpoint &&
        !(emitterMeshHandle == receiverMeshHandle);

    const ContactNodeParams params = readContactNodeParams(context.node);

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != NodePayloadType::Contact ||
            !hasValidContact) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        ContactData contactData{};
        contactData.pair.endpointA.payloadHandle = emitterInput->payloadHandle;
        contactData.pair.endpointA.meshHandle = emitterMeshHandle;
        contactData.pair.endpointB.payloadHandle = receiverInput->payloadHandle;
        contactData.pair.endpointB.meshHandle = receiverMeshHandle;
        contactData.emitterPayloadHash = emitterPayloadHash;
        contactData.receiverPayloadHash = receiverPayloadHash;
        contactData.pair.minNormalDot = static_cast<float>(params.minNormalDot);
        contactData.pair.contactRadius = static_cast<float>(params.contactRadius);
        contactData.pair.type =
            emitterInput->dataType == NodePayloadType::HeatSource &&
            receiverInput->dataType == NodePayloadType::HeatReceiver
            ? ContactCouplingType::SourceToReceiver
            : ContactCouplingType::ReceiverToReceiver;
        contactData.pair.hasValidContact = true;
        contactData.active = true;

        const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(contactData));
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeContact::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* emitterSocket = findInputSocket(context.node, "Emitter");
    const NodeGraphSocket* receiverSocket = findInputSocket(context.node, "Receiver");

    const EvaluatedSocketValue* emitterInputValue = emitterSocket ? readEvaluatedInput(context.node, emitterSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* emitterInput = readInputValue(emitterInputValue);
    if (emitterInput && valueTypeOf(emitterInput->dataType) != NodeGraphValueType::Mesh) {
        emitterInput = nullptr;
    }
    const EvaluatedSocketValue* receiverInputValue = receiverSocket ? readEvaluatedInput(context.node, receiverSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* receiverInput = readInputValue(receiverInputValue);
    if (receiverInput && valueTypeOf(receiverInput->dataType) != NodeGraphValueType::Mesh) {
        receiverInput = nullptr;
    }

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, emitterInput);
    NodeGraphHash::combineInputHash(outHash, receiverInput);

    const ContactNodeParams params = readContactNodeParams(context.node);
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.minNormalDot));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.contactRadius));

    const ContactCouplingType type =
        (emitterInput &&
         receiverInput &&
         emitterInput->dataType == NodePayloadType::HeatSource &&
         receiverInput->dataType == NodePayloadType::HeatReceiver)
        ? ContactCouplingType::SourceToReceiver
        : ContactCouplingType::ReceiverToReceiver;
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(type));
    return true;
}
