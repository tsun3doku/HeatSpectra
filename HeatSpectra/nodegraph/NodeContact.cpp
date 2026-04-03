#include "NodeContact.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphHash.hpp"
#include "NodePanelUtils.hpp"
#include "nodegraph/NodeGraphPayloadTypes.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/ContactPreviewStore.hpp"

#include <cstdint>

const char* NodeContact::typeId() const {
    return nodegraphtypes::Contact;
}

bool NodeContact::execute(NodeGraphKernelContext& context) const {
    ContactPreviewStore* const contactPreviewStore =
        context.executionState.services.contactPreviewStore;
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

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = NodePayloadType::Contact;
        outputValue.payloadHandle = {};

        ContactData contactData{};
        uint64_t emitterPayloadHash = 0;
        uint64_t receiverPayloadHash = 0;
        if (!emitterInput || !receiverInput ||
            emitterInput->payloadHandle.key == 0 ||
            receiverInput->payloadHandle.key == 0 ||
            emitterMeshHandle.key == 0 || receiverMeshHandle.key == 0 ||
            receiverInput->dataType != NodePayloadType::HeatReceiver ||
            (emitterInput->dataType != NodePayloadType::HeatSource &&
             emitterInput->dataType != NodePayloadType::HeatReceiver)) {
            if (contactPreviewStore) {
                contactPreviewStore->clearPreviewForNode(context.node.id.value);
            }
            contactData.payloadHash = NodeGraphHash::start();
            NodeGraphHash::combine(contactData.payloadHash, 0u);
            if (payloadRegistry) {
                const uint64_t payloadKey = makeSocketKey(
                    context.node.id,
                    context.node.outputs[outputIndex].id);
                outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(contactData));
            }
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        const ContactPairRole roleA =
            (emitterInput->dataType == NodePayloadType::HeatSource)
            ? ContactPairRole::Source
            : ContactPairRole::Receiver;
        contactData.pair.endpointA.role = roleA;
        contactData.pair.endpointA.payloadHandle = emitterInput->payloadHandle;
        contactData.pair.endpointA.meshHandle = emitterMeshHandle;
        contactData.pair.endpointB.role = ContactPairRole::Receiver;
        contactData.pair.endpointB.payloadHandle = receiverInput->payloadHandle;
        contactData.pair.endpointB.meshHandle = receiverMeshHandle;
        emitterPayloadHash = payloadHashForDataBlock(*emitterInput, payloadRegistry);
        receiverPayloadHash = payloadHashForDataBlock(*receiverInput, payloadRegistry);
        contactData.pair.minNormalDot = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node, nodegraphparams::contact::MinNormalDot, -0.65));
        contactData.pair.contactRadius = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node, nodegraphparams::contact::ContactRadius, 0.01));

        if (roleA == ContactPairRole::Source) {
            contactData.pair.kind = ContactCouplingType::SourceToReceiver;
        } else {
            contactData.pair.kind = ContactCouplingType::ReceiverToReceiver;
        }

        contactData.pair.hasValidContact =
            contactData.pair.endpointA.meshHandle.key != 0 &&
            contactData.pair.endpointB.meshHandle.key != 0 &&
            !(contactData.pair.endpointA.meshHandle == contactData.pair.endpointB.meshHandle);

        if (contactData.pair.hasValidContact) {
            contactData.active = true;
        }
        contactData.payloadHash = NodeGraphHash::start();
        NodeGraphHash::combine(contactData.payloadHash, static_cast<uint64_t>(contactData.active ? 1u : 0u));
        NodeGraphHash::combine(contactData.payloadHash, emitterPayloadHash);
        NodeGraphHash::combine(contactData.payloadHash, receiverPayloadHash);
        NodeGraphHash::combine(contactData.payloadHash, static_cast<uint64_t>(contactData.pair.endpointA.role));
        NodeGraphHash::combine(contactData.payloadHash, static_cast<uint64_t>(contactData.pair.endpointB.role));
        NodeGraphHash::combine(contactData.payloadHash, static_cast<uint64_t>(contactData.pair.kind));
        NodeGraphHash::combineFloat(contactData.payloadHash, contactData.pair.minNormalDot);
        NodeGraphHash::combineFloat(contactData.payloadHash, contactData.pair.contactRadius);
        NodeGraphHash::combine(contactData.payloadHash, static_cast<uint64_t>(contactData.pair.hasValidContact ? 1u : 0u));

        if (contactPreviewStore) {
            contactPreviewStore->clearPreviewForNode(context.node.id.value);
        }
        if (payloadRegistry) {
            const uint64_t payloadKey = makeSocketKey(
                context.node.id,
                context.node.outputs[outputIndex].id);
            outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(contactData));
        }
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}

bool NodeContact::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* emitterSocket = findInputSocket(context.node, "Emitter");
    const NodeGraphSocket* receiverSocket = findInputSocket(context.node, "Receiver");
    const EvaluatedSocketValue* emitterInputValue =
        emitterSocket ? readEvaluatedInput(context.node, emitterSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* emitterInput = readInputValue(emitterInputValue);
    const EvaluatedSocketValue* receiverInputValue =
        receiverSocket ? readEvaluatedInput(context.node, receiverSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* receiverInput = readInputValue(receiverInputValue);

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    if (!emitterInput) {
        NodeGraphHash::combine(outHash, 0u);
    } else {
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(emitterInput->dataType));
        NodeGraphHash::combine(outHash, emitterInput->payloadHandle.key);
        NodeGraphHash::combine(outHash, emitterInput->payloadHandle.revision);
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(emitterInput->payloadHandle.count));
    }

    if (!receiverInput) {
        NodeGraphHash::combine(outHash, 0u);
    } else {
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(receiverInput->dataType));
        NodeGraphHash::combine(outHash, receiverInput->payloadHandle.key);
        NodeGraphHash::combine(outHash, receiverInput->payloadHandle.revision);
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(receiverInput->payloadHandle.count));
    }

    const ContactPairRole roleA =
        (emitterInput && emitterInput->dataType == NodePayloadType::HeatSource)
        ? ContactPairRole::Source
        : ContactPairRole::Receiver;
    const ContactCouplingType kind =
        (roleA == ContactPairRole::Source)
        ? ContactCouplingType::SourceToReceiver
        : ContactCouplingType::ReceiverToReceiver;
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(kind));

    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::contact::MinNormalDot, -0.65)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::contact::ContactRadius, 0.01)));
    return true;
}
