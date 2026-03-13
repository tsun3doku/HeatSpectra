#include "NodeContactPair.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"
#include "heat/ContactSystemController.hpp"

#include <cstdint>

const char* NodeContactPair::typeId() const {
    return nodegraphtypes::ContactPair;
}

bool NodeContactPair::execute(NodeGraphKernelContext& context) const {
    const NodeDataBlock* emitterInput = nullptr;
    const NodeDataBlock* receiverInput = nullptr;
    const bool computeRequested = getBoolParamValue(
        context.node,
        nodegraphparams::contactpair::ComputeRequested,
        false);
    if (computeRequested) {
        setBoolParameter(
            context.executionState.bridge,
            context.node.id,
            nodegraphparams::contactpair::ComputeRequested,
            false);
    }

    if (!context.node.inputs.empty()) {
        emitterInput = resolveInputValueForSocket(
            context.node,
            context.node.inputs[0].id,
            context.executionState);
    }
    if (context.node.inputs.size() > 1) {
        receiverInput = resolveInputValueForSocket(
            context.node,
            context.node.inputs[1].id,
            context.executionState);
    }

    for (NodeDataBlock& outputValue : context.outputs) {
        outputValue.dataType = NodeDataType::ContactPair;
        outputValue.contactPairData = ContactPairData{};

        ContactPairData& contactPairData = outputValue.contactPairData;
        contactPairData.computeRequested = computeRequested;
        if (!emitterInput || !receiverInput ||
            emitterInput->geometry.modelId == 0 ||
            receiverInput->geometry.modelId == 0) {
            contactPairData.hasValidContact = false;
            refreshNodeDataBlockMetadata(outputValue);
            continue;
        }

        if (receiverInput->dataType != NodeDataType::HeatReceiver) {
            contactPairData.hasValidContact = false;
            refreshNodeDataBlockMetadata(outputValue);
            continue;
        }

        const ContactPairRole roleA =
            (emitterInput->dataType == NodeDataType::HeatSource)
            ? ContactPairRole::Source
            : (emitterInput->dataType == NodeDataType::HeatReceiver)
            ? ContactPairRole::Receiver
            : ContactPairRole::Receiver;
        if (emitterInput->dataType != NodeDataType::HeatSource &&
            emitterInput->dataType != NodeDataType::HeatReceiver) {
            contactPairData.hasValidContact = false;
            refreshNodeDataBlockMetadata(outputValue);
            continue;
        }

        const ContactPairRole roleB = ContactPairRole::Receiver;

        contactPairData.modelIdA = emitterInput->geometry.modelId;
        contactPairData.roleA = roleA;
        contactPairData.geometryA = emitterInput->geometry;
        contactPairData.modelIdB = receiverInput->geometry.modelId;
        contactPairData.roleB = roleB;
        contactPairData.geometryB = receiverInput->geometry;
        contactPairData.minNormalDot = static_cast<float>(getFloatParamValue(
            context.node, nodegraphparams::contactpair::MinNormalDot, -0.65));
        contactPairData.contactRadius = static_cast<float>(getFloatParamValue(
            context.node, nodegraphparams::contactpair::ContactRadius, 0.01));

        if (roleA == ContactPairRole::Source) {
            contactPairData.kind = ContactPairKind::SourceToReceiver;
            contactPairData.emitterModelId = contactPairData.modelIdA;
            contactPairData.receiverModelId = contactPairData.modelIdB;
        } else {
            contactPairData.kind = ContactPairKind::ReceiverToReceiver;
            // One-way bias follows node input direction: Emitter socket emits, Receiver socket receives.
            contactPairData.emitterModelId = contactPairData.modelIdA;
            contactPairData.receiverModelId = contactPairData.modelIdB;
        }

        contactPairData.hasValidContact =
            contactPairData.emitterModelId != 0 &&
            contactPairData.receiverModelId != 0 &&
            contactPairData.emitterModelId != contactPairData.receiverModelId;
        contactPairData.contactPairs.clear();
        ContactSystemController* const contactSystemController =
            context.executionState.services.contactSystemController;
        if (contactPairData.hasValidContact && contactSystemController) {
            const ContactCouplingKind couplingKind =
                (contactPairData.kind == ContactPairKind::ReceiverToReceiver)
                ? ContactCouplingKind::ReceiverToReceiver
                : ContactCouplingKind::SourceToReceiver;
            contactSystemController->updatePreviewForNodeModels(
                context.node.id.value,
                couplingKind,
                contactPairData.emitterModelId,
                contactPairData.receiverModelId,
                contactPairData.minNormalDot,
                contactPairData.contactRadius,
                contactPairData.contactPairs,
                computeRequested);
        }
        refreshNodeDataBlockMetadata(outputValue);
    }

    return computeRequested;
}

uint64_t NodeContactPair::makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

const NodeDataBlock* NodeContactPair::resolveInputValueForSocket(
    const NodeGraphNode& node,
    NodeGraphSocketId inputSocketId,
    const NodeGraphKernelExecutionState& executionState) {
    const auto edgeIt = executionState.incomingEdgeByInputSocket.find(makeSocketKey(node.id, inputSocketId));
    if (edgeIt == executionState.incomingEdgeByInputSocket.end() || !edgeIt->second) {
        return nullptr;
    }

    const NodeGraphEdge& edge = *edgeIt->second;
    const auto valueIt = executionState.outputValueBySocket.find(makeSocketKey(edge.fromNode, edge.fromSocket));
    if (valueIt == executionState.outputValueBySocket.end()) {
        return nullptr;
    }

    return &valueIt->second;
}

double NodeContactPair::getFloatParamValue(const NodeGraphNode& node, uint32_t parameterId, double defaultValue) {
    double value = defaultValue;
    if (tryGetNodeParamFloat(node, parameterId, value)) {
        return value;
    }
    return defaultValue;
}

bool NodeContactPair::getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue) {
    bool value = defaultValue;
    if (tryGetNodeParamBool(node, parameterId, value)) {
        return value;
    }
    return defaultValue;
}

bool NodeContactPair::setBoolParameter(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value) {
    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Bool;
    parameter.boolValue = value;
    return bridge.setNodeParameter(nodeId, parameter);
}
