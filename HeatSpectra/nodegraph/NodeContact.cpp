#include "NodeContact.hpp"
#include "NodeGraphRegistry.hpp"
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
    const NodeDataBlock* emitterInput = nullptr;
    if (emitterSocket) {
        emitterInput = readInput(context.node, emitterSocket->id, context.executionState);
    }
    const NodeDataBlock* receiverInput = nullptr;
    if (receiverSocket) {
        receiverInput = readInput(context.node, receiverSocket->id, context.executionState);
    }

    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    const GeometryData* emitterGeometry = nullptr;
    NodeDataHandle emitterGeometryHandle{};
    if (payloadRegistry && emitterInput) {
        if (emitterInput->dataType == NodeDataType::HeatSource) {
            const HeatSourceData* heatSource = payloadRegistry->get<HeatSourceData>(emitterInput->payloadHandle);
            if (heatSource) {
                emitterGeometryHandle = heatSource->geometryHandle;
                emitterGeometry = payloadRegistry->get<GeometryData>(emitterGeometryHandle);
            }
        } else if (emitterInput->dataType == NodeDataType::HeatReceiver) {
            const HeatReceiverData* heatReceiver = payloadRegistry->get<HeatReceiverData>(emitterInput->payloadHandle);
            if (heatReceiver) {
                emitterGeometryHandle = heatReceiver->geometryHandle;
                emitterGeometry = payloadRegistry->get<GeometryData>(emitterGeometryHandle);
            }
        } else {
            emitterGeometryHandle = emitterInput->payloadHandle;
            emitterGeometry = payloadRegistry->get<GeometryData>(emitterGeometryHandle);
        }
    }

    const GeometryData* receiverGeometry = nullptr;
    NodeDataHandle receiverGeometryHandle{};
    if (payloadRegistry && receiverInput && receiverInput->dataType == NodeDataType::HeatReceiver) {
        const HeatReceiverData* heatReceiver = payloadRegistry->get<HeatReceiverData>(receiverInput->payloadHandle);
        if (heatReceiver) {
            receiverGeometryHandle = heatReceiver->geometryHandle;
            receiverGeometry = payloadRegistry->get<GeometryData>(receiverGeometryHandle);
        }
    }

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = NodeDataType::Contact;
        outputValue.payloadHandle = {};

        ContactData contactData{};
        if (!emitterInput || !receiverInput || !emitterGeometry || !receiverGeometry ||
            emitterGeometryHandle.key == 0 || receiverGeometryHandle.key == 0 ||
            receiverInput->dataType != NodeDataType::HeatReceiver ||
            (emitterInput->dataType != NodeDataType::HeatSource &&
             emitterInput->dataType != NodeDataType::HeatReceiver)) {
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
            continue;
        }

        ContactBindingData binding{};
        const ContactPairRole roleA =
            (emitterInput->dataType == NodeDataType::HeatSource)
            ? ContactPairRole::Source
            : ContactPairRole::Receiver;
        binding.pair.endpointA.role = roleA;
        binding.pair.endpointA.payloadHandle = emitterInput->payloadHandle;
        binding.pair.endpointA.geometryHandle = emitterGeometryHandle;
        binding.pair.endpointB.role = ContactPairRole::Receiver;
        binding.pair.endpointB.payloadHandle = receiverInput->payloadHandle;
        binding.pair.endpointB.geometryHandle = receiverGeometryHandle;
        binding.pair.minNormalDot = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node, nodegraphparams::contact::MinNormalDot, -0.65));
        binding.pair.contactRadius = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node, nodegraphparams::contact::ContactRadius, 0.01));

        if (roleA == ContactPairRole::Source) {
            binding.pair.kind = ContactCouplingType::SourceToReceiver;
        } else {
            binding.pair.kind = ContactCouplingType::ReceiverToReceiver;
        }

        binding.pair.hasValidContact =
            binding.pair.endpointA.geometryHandle.key != 0 &&
            binding.pair.endpointB.geometryHandle.key != 0 &&
            !(binding.pair.endpointA.geometryHandle == binding.pair.endpointB.geometryHandle);

        if (binding.pair.hasValidContact) {
            contactData.bindings.push_back(binding);
            contactData.active = true;
        }

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
    const NodeDataBlock* emitterInput = nullptr;
    if (emitterSocket) {
        emitterInput = readInput(context.node, emitterSocket->id, context.executionState);
    }
    const NodeDataBlock* receiverInput = nullptr;
    if (receiverSocket) {
        receiverInput = readInput(context.node, receiverSocket->id, context.executionState);
    }

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
        (emitterInput && emitterInput->dataType == NodeDataType::HeatSource)
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
