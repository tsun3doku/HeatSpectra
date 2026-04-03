#include "NodeHeatSolve.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphCompiler.hpp"
#include "NodeGraphHash.hpp"
#include "NodeHeatMaterialPresets.hpp"
#include "NodePanelUtils.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <unordered_set>
#include <vector>

std::vector<HeatMaterialBindingEntry> NodeHeatSolve::parseMaterialBindings(const std::string& serializedBindings) {
    std::vector<HeatMaterialBindingEntry> parsedBindings;
    if (serializedBindings.empty()) {
        return parsedBindings;
    }

    std::stringstream listStream(serializedBindings);
    std::string token;
    std::unordered_set<std::string> seenGroups;
    while (std::getline(listStream, token, ';')) {
        token = NodePanelUtils::trimCopy(token);
        if (token.empty()) {
            continue;
        }

        const std::size_t separatorIndex = token.find('=');
        if (separatorIndex == std::string::npos) {
            continue;
        }

        std::string groupName = NodePanelUtils::trimCopy(token.substr(0, separatorIndex));
        std::string presetName = NodePanelUtils::trimCopy(token.substr(separatorIndex + 1));
        if (groupName.empty() || presetName.empty()) {
            continue;
        }

        std::string normalizedGroupName;
        normalizedGroupName.reserve(groupName.size());
        for (char character : groupName) {
            const unsigned char u = static_cast<unsigned char>(character);
            if (std::isspace(u) != 0) {
                continue;
            }
            normalizedGroupName.push_back(static_cast<char>(std::tolower(u)));
        }
        if (normalizedGroupName.empty() || !seenGroups.insert(normalizedGroupName).second) {
            continue;
        }

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        if (!tryResolveHeatPresetId(presetName, presetId)) {
            continue;
        }

        HeatMaterialBindingEntry binding{};
        binding.groupName = std::move(groupName);
        binding.presetId = presetId;
        parsedBindings.push_back(std::move(binding));
    }

    return parsedBindings;
}

namespace {

void appendSourceHandlesFromContact(
    const ContactData& contact,
    std::vector<NodeDataHandle>& sourceHandles) {
    const ContactPairData& pair = contact.pair;
    if (!pair.hasValidContact) {
        return;
    }

    const ContactPairEndpoint* sourceEndpoint = nullptr;
    if (pair.endpointA.role == ContactPairRole::Source) {
        sourceEndpoint = &pair.endpointA;
    } else if (pair.endpointB.role == ContactPairRole::Source) {
        sourceEndpoint = &pair.endpointB;
    }

    if (!sourceEndpoint || sourceEndpoint->payloadHandle.key == 0) {
        return;
    }

    sourceHandles.push_back(sourceEndpoint->payloadHandle);
}

const VoronoiData* readVoronoiInput(
    const NodeGraphNode& node,
    const NodeGraphKernelExecutionState& executionState,
    const NodePayloadRegistry* payloadRegistry) {
    if (!payloadRegistry) {
        return nullptr;
    }

    const NodeGraphSocket* inputSocket = findInputSocket(node, NodeGraphValueType::Volume);
    if (!inputSocket) {
        return nullptr;
    }

    const NodeDataBlock* inputValue = readInputValue(
        readEvaluatedInput(node, inputSocket->id, executionState));
    if (!inputValue) {
        return nullptr;
    }
    if (inputValue->dataType != NodePayloadType::Voronoi) {
        return nullptr;
    }

    const VoronoiData* voronoi = payloadRegistry->get<VoronoiData>(inputValue->payloadHandle);
    if (!voronoi || !voronoi->active) {
        return nullptr;
    }
    return voronoi;
}

const ContactData* readContactInput(
    const NodeGraphNode& node,
    const NodeGraphKernelExecutionState& executionState,
    const NodePayloadRegistry* payloadRegistry) {
    if (!payloadRegistry) {
        return nullptr;
    }

    const NodeGraphSocket* inputSocket = findInputSocket(node, NodeGraphValueType::Field);
    if (!inputSocket) {
        return nullptr;
    }

    const NodeDataBlock* inputValue = readInputValue(
        readEvaluatedInput(node, inputSocket->id, executionState));
    if (!inputValue) {
        return nullptr;
    }
    if (inputValue->dataType != NodePayloadType::Contact) {
        return nullptr;
    }

    const ContactData* contact = payloadRegistry->get<ContactData>(inputValue->payloadHandle);
    if (!contact || !contact->active || !contact->pair.hasValidContact) {
        return nullptr;
    }
    return contact;
}

} // namespace

const char* NodeHeatSolve::typeId() const {
    return nodegraphtypes::HeatSolve;
}

bool NodeHeatSolve::execute(NodeGraphKernelContext& context) const {
    std::vector<NodeDataHandle> sourceHandles;
    std::vector<NodeDataHandle> receiverMeshHandles;
    std::vector<HeatMaterialBindingEntry> materialBindings;

    const bool resetRequested = NodePanelUtils::readBoolParam(context.node, nodegraphparams::heatsolve::ResetRequested, false);

    const NodeGraphNodeId selectedNodeId =
        selectHeatSolveNode(context.executionState.state, context.executionState);
    if (!selectedNodeId.isValid() || selectedNodeId != context.node.id) {
        populateOutputPayloads(
            context,
            sourceHandles,
            receiverMeshHandles,
            materialBindings,
            0,
            0,
            false,
            false,
            false);
        return false;
    }

    const NodePayloadRegistry* payloadRegistry = context.executionState.services.payloadRegistry;
    const VoronoiData* voronoiInput = readVoronoiInput(
        context.node,
        context.executionState,
        payloadRegistry);
    const ContactData* contactInput = readContactInput(
        context.node,
        context.executionState,
        payloadRegistry);

    if (!voronoiInput || !contactInput) {
        populateOutputPayloads(
            context,
            sourceHandles,
            receiverMeshHandles,
            materialBindings,
            0,
            0,
            false,
            false,
            false);
        return false;
    }

    appendSourceHandlesFromContact(*contactInput, sourceHandles);
    receiverMeshHandles = voronoiInput->receiverMeshHandles;
    materialBindings = parseMaterialBindings(
        NodePanelUtils::readStringParam(context.node, nodegraphparams::heatsolve::MaterialBindings));

    const bool wantsPaused = resetRequested
        ? false
        : NodePanelUtils::readBoolParam(context.node, nodegraphparams::heatsolve::Paused, false);
    populateOutputPayloads(
        context,
        sourceHandles,
        receiverMeshHandles,
        materialBindings,
        voronoiInput->payloadHash,
        contactInput->payloadHash,
        true,
        wantsPaused,
        resetRequested);
    return false;
}

void NodeHeatSolve::populateOutputPayloads(
    NodeGraphKernelContext& context,
    const std::vector<NodeDataHandle>& sourceHandles,
    const std::vector<NodeDataHandle>& receiverMeshHandles,
    const std::vector<HeatMaterialBindingEntry>& materialBindings,
    uint64_t voronoiPayloadHash,
    uint64_t contactPayloadHash,
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
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
        if (outputSocket.contract.producedPayloadType == NodePayloadType::Heat) {
            HeatData heatData{};
            heatData.sourceHandles = sourceHandles;
            heatData.receiverMeshHandles = receiverMeshHandles;
            heatData.materialBindings = materialBindings;
            heatData.active = active;
            heatData.paused = paused;
            heatData.resetRequested = resetRequested;
            heatData.payloadHash = NodeGraphHash::start();
            NodeGraphHash::combine(heatData.payloadHash, voronoiPayloadHash);
            NodeGraphHash::combine(heatData.payloadHash, contactPayloadHash);
            NodeGraphHash::combine(heatData.payloadHash, static_cast<uint64_t>(heatData.active ? 1u : 0u));
            NodeGraphHash::combine(heatData.payloadHash, static_cast<uint64_t>(heatData.paused ? 1u : 0u));
            NodeGraphHash::combine(heatData.payloadHash, static_cast<uint64_t>(heatData.resetRequested ? 1u : 0u));
            NodeGraphHash::combine(heatData.payloadHash, static_cast<uint64_t>(heatData.materialBindings.size()));
            for (const HeatMaterialBindingEntry& binding : heatData.materialBindings) {
                NodeGraphHash::combineString(heatData.payloadHash, binding.groupName);
                NodeGraphHash::combine(heatData.payloadHash, static_cast<uint64_t>(binding.presetId));
            }
            outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(heatData));
        }

        updateDataBlockMetadata(outputValue, payloadRegistry);
    }
}

bool NodeHeatSolve::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphNodeId selectedNodeId =
        selectHeatSolveNode(context.executionState.state, context.executionState);
    if (!selectedNodeId.isValid() || selectedNodeId != context.node.id) {
        return false;
    }

    outHash = NodeGraphHash::start();

    const NodePayloadRegistry* payloadRegistry = context.executionState.services.payloadRegistry;
    const VoronoiData* voronoi = readVoronoiInput(context.node, context.executionState, payloadRegistry);
    const ContactData* contact = readContactInput(context.node, context.executionState, payloadRegistry);
    if (!voronoi || !contact) {
        return false;
    }

    const NodeGraphSocket* voronoiSocket = findInputSocket(context.node, NodeGraphValueType::Volume);
    const NodeGraphSocket* contactSocket = findInputSocket(context.node, NodeGraphValueType::Field);
    for (const NodeGraphSocket* inputSocket : {voronoiSocket, contactSocket}) {
        if (!inputSocket) {
            continue;
        }

        const NodeDataBlock* inputValue = readInputValue(
            readEvaluatedInput(context.node, inputSocket->id, context.executionState));
        if (!inputValue) {
            continue;
        }

        NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputValue->dataType));
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.key);
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.revision);
    }

    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineFloat(outHash, voronoi->params.cellSize);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(voronoi->params.voxelResolution));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(contact->pair.kind));

    NodeGraphHash::combineString(outHash, NodePanelUtils::readStringParam(context.node, nodegraphparams::heatsolve::MaterialBindings));
    std::vector<NodeDataHandle> sourceHandles;
    appendSourceHandlesFromContact(*contact, sourceHandles);
    for (const NodeDataHandle& sourceHandle : sourceHandles) {
        if (sourceHandle.key == 0) {
            continue;
        }

        NodeGraphHash::combine(outHash, static_cast<uint64_t>(NodePayloadType::HeatSource));
        NodeGraphHash::combine(outHash, sourceHandle.key);
        NodeGraphHash::combine(outHash, sourceHandle.revision);
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(sourceHandle.count));
    }

    const bool resetRequested = NodePanelUtils::readBoolParam(context.node, nodegraphparams::heatsolve::ResetRequested, false);
    const bool wantsPaused = NodePanelUtils::readBoolParam(context.node, nodegraphparams::heatsolve::Paused, false);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(wantsPaused));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(resetRequested));

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

        bool enabled = false;
        if (!tryGetNodeParamBool(node, nodegraphparams::heatsolve::Enabled, enabled) || !enabled) {
            continue;
        }

        if (selectedNodeId.isValid()) {
            return {};
        }

        selectedNodeId = node.id;
    }

    return selectedNodeId;
}
