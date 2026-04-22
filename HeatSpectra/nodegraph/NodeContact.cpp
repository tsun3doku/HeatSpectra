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
    if (payloadRegistry && emitterInput) {
        if (emitterInput->dataType == NodePayloadType::HeatSource) {
            const HeatSourceData* heatSource = payloadRegistry->get<HeatSourceData>(emitterInput->payloadHandle);
            if (heatSource) {
                emitterMeshHandle = heatSource->meshHandle;
            }
        } else if (emitterInput->dataType == NodePayloadType::HeatReceiver) {
            const HeatReceiverData* heatReceiver = payloadRegistry->get<HeatReceiverData>(emitterInput->payloadHandle);
            if (heatReceiver) {
                emitterMeshHandle = heatReceiver->meshHandle;
            }
        }
    }

    NodeDataHandle receiverMeshHandle{};
    if (payloadRegistry && receiverInput && receiverInput->dataType == NodePayloadType::HeatReceiver) {
        const HeatReceiverData* heatReceiver = payloadRegistry->get<HeatReceiverData>(receiverInput->payloadHandle);
        if (heatReceiver) {
            receiverMeshHandle = heatReceiver->meshHandle;
        }
    }

    const ContactNodeParams params = readContactNodeParams(context.node);

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedPayloadType;
        outputValue.payloadHandle = {};

        if (!emitterInput || !receiverInput ||
            emitterInput->payloadHandle.key == 0 ||
            receiverInput->payloadHandle.key == 0 ||
            emitterMeshHandle.key == 0 || receiverMeshHandle.key == 0 ||
            receiverInput->dataType != NodePayloadType::HeatReceiver ||
            (emitterInput->dataType != NodePayloadType::HeatSource &&
             emitterInput->dataType != NodePayloadType::HeatReceiver)) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        ContactData contactData{};

        contactData.pair.endpointA.payloadHandle = emitterInput->payloadHandle;
        contactData.pair.endpointA.meshHandle = emitterMeshHandle;
        contactData.pair.endpointB.payloadHandle = receiverInput->payloadHandle;
        contactData.pair.endpointB.meshHandle = receiverMeshHandle;
        contactData.emitterPayloadHash = payloadRegistry->resolvePayloadHash(emitterInput->dataType, emitterInput->payloadHandle);
        contactData.receiverPayloadHash = payloadRegistry->resolvePayloadHash(receiverInput->dataType, receiverInput->payloadHandle);
        contactData.pair.minNormalDot = static_cast<float>(params.minNormalDot);
        contactData.pair.contactRadius = static_cast<float>(params.contactRadius);

        if (emitterInput->dataType == NodePayloadType::HeatSource) {
            contactData.pair.type = ContactCouplingType::SourceToReceiver;
        } else {
            contactData.pair.type = ContactCouplingType::ReceiverToReceiver;
        }

        contactData.pair.hasValidContact =
            contactData.pair.endpointA.meshHandle.key != 0 &&
            contactData.pair.endpointB.meshHandle.key != 0 &&
            !(contactData.pair.endpointA.meshHandle == contactData.pair.endpointB.meshHandle);

        if (contactData.pair.hasValidContact) {
            contactData.active = true;
        }

        if (payloadRegistry) {
            const uint64_t payloadKey = makeSocketKey(
                context.node.id,
                context.node.outputs[outputIndex].id);
            outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(contactData));
        }
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeContact::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* emitterSocket = findInputSocket(context.node, "Emitter");
    const NodeGraphSocket* receiverSocket = findInputSocket(context.node, "Receiver");

    const EvaluatedSocketValue* emitterInputValue = emitterSocket ? readEvaluatedInput(context.node, emitterSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* emitterInput = readInputValue(emitterInputValue);
    const EvaluatedSocketValue* receiverInputValue = receiverSocket ? readEvaluatedInput(context.node, receiverSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* receiverInput = readInputValue(receiverInputValue);

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, emitterInput);
    NodeGraphHash::combineInputHash(outHash, receiverInput);

    const ContactNodeParams params = readContactNodeParams(context.node);
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.minNormalDot));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.contactRadius));

    const ContactCouplingType type =
        (emitterInput && emitterInput->dataType == NodePayloadType::HeatSource)
        ? ContactCouplingType::SourceToReceiver
        : ContactCouplingType::ReceiverToReceiver;
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(type));
    return true;
}
