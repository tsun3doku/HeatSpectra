#include "NodeHeatSolve.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodeHeatSolveParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

#include <cstdint>
#include <vector>

namespace {

void appendSourceHandlesFromContact(
    const ContactData& contact,
    std::vector<NodeDataHandle>& sourceHandles) {
    const ContactPairData& pair = contact.pair;
    if (!pair.hasValidContact) {
        return;
    }

    const ContactPairEndpoint* sourceEndpoint = nullptr;
    if (pair.type == ContactCouplingType::SourceToReceiver) {
        sourceEndpoint = &pair.endpointA;
    }

    if (!sourceEndpoint || sourceEndpoint->payloadHandle.key == 0) {
        return;
    }

    sourceHandles.push_back(sourceEndpoint->payloadHandle);
}

} // namespace

const char* NodeHeatSolve::typeId() const {
    return nodegraphtypes::HeatSolve;
}

void NodeHeatSolve::execute(NodeGraphKernelContext& context) const {
    std::vector<NodeDataHandle> sourceHandles;
    std::vector<NodeDataHandle> receiverMeshHandles;
    std::vector<HeatMaterialBinding> materialBindings;
    const HeatSolveNodeParams params = readHeatSolveNodeParams(context.node);

    const NodeGraphNodeId activeNodeId =
        selectHeatSolveNode(context.executionState.state, context.executionState);
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    // Voronoi input (Volume socket)
    const NodeGraphSocket* voronoiSocket = findInputSocket(context.node, NodeGraphValueType::Volume);
    const EvaluatedSocketValue* voronoiEval = voronoiSocket
        ? readEvaluatedInput(context.node, voronoiSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* voronoiInputBlock = readInputValue(voronoiEval);
    const VoronoiData* voronoiInput = nullptr;
    if (payloadRegistry && voronoiInputBlock && voronoiInputBlock->payloadHandle.key != 0 &&
        voronoiInputBlock->dataType == NodePayloadType::Voronoi) {
        voronoiInput = payloadRegistry->get<VoronoiData>(voronoiInputBlock->payloadHandle);
        if (voronoiInput && !voronoiInput->active) { voronoiInput = nullptr; }
    }

    // Contact input (Field socket)
    const NodeGraphSocket* contactSocket = findInputSocket(context.node, NodeGraphValueType::Field);
    const EvaluatedSocketValue* contactEval = contactSocket
        ? readEvaluatedInput(context.node, contactSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* contactInputBlock = readInputValue(contactEval);
    const ContactData* contactInput = nullptr;
    if (payloadRegistry && contactInputBlock && contactInputBlock->payloadHandle.key != 0 &&
        contactInputBlock->dataType == NodePayloadType::Contact) {
        contactInput = payloadRegistry->get<ContactData>(contactInputBlock->payloadHandle);
        if (contactInput && (!contactInput->active || !contactInput->pair.hasValidContact)) { contactInput = nullptr; }
    }

    if (!voronoiInput || !contactInput) {
        populateOutputPayloads(
            context,
            sourceHandles,
            receiverMeshHandles,
            materialBindings,
            0,
            0,
            16000.0f,
            false,
            false,
            false);
        return;
    }

    appendSourceHandlesFromContact(*contactInput, sourceHandles);
    receiverMeshHandles = voronoiInput->receiverMeshHandles;
    materialBindings = makeHeatPayloadMaterialBindings(params);

    const bool active = activeNodeId.isValid() && activeNodeId == context.node.id;
    const bool wantsPaused = params.resetRequested
        ? false
        : params.paused;
    populateOutputPayloads(
        context,
        sourceHandles,
        receiverMeshHandles,
        materialBindings,
        voronoiInput->payloadHash,
        contactInput->payloadHash,
        static_cast<float>(params.contactThermalConductance),
        active,
        active ? wantsPaused : false,
        active ? params.resetRequested : false);
}

void NodeHeatSolve::populateOutputPayloads(
    NodeGraphKernelContext& context,
    const std::vector<NodeDataHandle>& sourceHandles,
    const std::vector<NodeDataHandle>& receiverMeshHandles,
    const std::vector<HeatMaterialBinding>& materialBindings,
    uint64_t voronoiPayloadHash,
    uint64_t contactPayloadHash,
    float contactThermalConductance,
    bool active,
    bool paused,
    bool resetRequested) {
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size() && outputIndex < context.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
        if (outputSocket.contract.producedPayloadType == NodePayloadType::Heat) {
            HeatData heatData{};
            heatData.voronoiPayloadHash = voronoiPayloadHash;
            heatData.contactPayloadHash = contactPayloadHash;
            heatData.sourceHandles = sourceHandles;
            heatData.receiverMeshHandles = receiverMeshHandles;
            heatData.materialBindings = materialBindings;
            heatData.contactThermalConductance = contactThermalConductance;
            heatData.active = active;
            heatData.paused = paused;
            heatData.resetRequested = resetRequested;
            outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(heatData));
        }

        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeHeatSolve::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphNodeId activeNodeId =
        selectHeatSolveNode(context.executionState.state, context.executionState);
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    const NodeGraphSocket* voronoiSocket = findInputSocket(context.node, NodeGraphValueType::Volume);
    const NodeGraphSocket* contactSocket = findInputSocket(context.node, NodeGraphValueType::Field);
    const NodeDataBlock* voronoiInput = voronoiSocket
        ? readInputValue(readEvaluatedInput(context.node, voronoiSocket->id, context.executionState))
        : nullptr;
    const NodeDataBlock* contactInput = contactSocket
        ? readInputValue(readEvaluatedInput(context.node, contactSocket->id, context.executionState))
        : nullptr;

    NodeGraphHash::combineInputHash(outHash, voronoiInput);
    NodeGraphHash::combineInputHash(outHash, contactInput);

    const HeatSolveNodeParams params = readHeatSolveNodeParams(context.node);
    const std::vector<HeatMaterialBinding> materialBindings = makeHeatPayloadMaterialBindings(params);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(materialBindings.size()));
    for (const HeatMaterialBinding& binding : materialBindings) {
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(binding.receiverModelNodeId));
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(binding.presetId));
    }
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.contactThermalConductance));
    const bool active = activeNodeId.isValid() && activeNodeId == context.node.id;
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(active ? 1u : 0u));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(active && params.paused ? 1u : 0u));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(active && params.resetRequested ? 1u : 0u));

    return true;
}

NodeGraphNodeId NodeHeatSolve::selectHeatSolveNode(
    const NodeGraphState& state,
    const NodeGraphKernelExecutionState& executionState) {
    (void)executionState;
    NodeGraphNodeId selectedNodeId{};
    for (const NodeGraphNode& node : state.nodes) {
        if (getNodeTypeId(node.typeId) != nodegraphtypes::HeatSolve) {
            continue;
        }

        const HeatSolveNodeParams params = readHeatSolveNodeParams(node);
        if (!params.enabled) {
            continue;
        }

        if (selectedNodeId.isValid()) {
            return {};
        }

        selectedNodeId = node.id;
    }

    return selectedNodeId;
}
