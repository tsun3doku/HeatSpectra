#include "NodeHeatSolve.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphExecutionPlanner.hpp"
#include "NodeHeatMaterialPresets.hpp"
#include "NodePanelUtils.hpp"
#include "NodeSolverController.hpp"
#include "heat/HeatContactParams.hpp"
#include "heat/HeatSolveParams.hpp"
#include "heat/HeatSystemPresets.hpp"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::vector<HeatMaterialBindingEntry> parseMaterialBindings(const std::string& serializedBindings) {
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

std::unordered_map<uint32_t, HeatContactParams> parseContactBindings(const std::string& serializedBindings) {
    std::unordered_map<uint32_t, HeatContactParams> parsedBindings;
    if (serializedBindings.empty()) {
        return parsedBindings;
    }

    std::stringstream listStream(serializedBindings);
    std::string token;
    while (std::getline(listStream, token, ';')) {
        token = NodePanelUtils::trimCopy(token);
        if (token.empty()) {
            continue;
        }

        const std::size_t separatorIndex = token.find('=');
        if (separatorIndex == std::string::npos) {
            continue;
        }

        uint32_t socketId = 0;
        if (!NodePanelUtils::tryParseUint32Id(token.substr(0, separatorIndex), socketId) || socketId == 0) {
            continue;
        }

        try {
            HeatContactParams params{};
            params.thermalConductance = std::stof(NodePanelUtils::trimCopy(token.substr(separatorIndex + 1)));
            parsedBindings[socketId] = params;
        } catch (...) {
            continue;
        }
    }

    return parsedBindings;
}

} // namespace

namespace {

double getFloatParamValue(const NodeGraphNode& node, uint32_t parameterId, double defaultValue) {
    double value = defaultValue;
    if (tryGetNodeParamFloat(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

int getIntParamValue(const NodeGraphNode& node, uint32_t parameterId, int defaultValue) {
    int64_t value = defaultValue;
    if (tryGetNodeParamInt(node, parameterId, value)) {
        return static_cast<int>(value);
    }

    return defaultValue;
}

} // namespace

const char* NodeHeatSolve::typeId() const {
    return nodegraphtypes::HeatSolve;
}

bool NodeHeatSolve::execute(NodeGraphKernelContext& context) const {
    NodeGraphBridge& bridge = context.executionState.bridge;
    NodeSolverController* const solverController = context.executionState.services.nodeSolverController;
    if (!solverController) {
        return false;
    }

    bool executed = false;
    const bool resetRequested = getBoolParamValue(context.node, nodegraphparams::heatsolve::ResetRequested, false);
    if (resetRequested) {
        solverController->resetHeatSolve();
        setBoolParameter(bridge, context.node.id, nodegraphparams::heatsolve::ResetRequested, false);
        setBoolParameter(bridge, context.node.id, nodegraphparams::heatsolve::Paused, false);
        executed = true;
    }

    const NodeGraphNodeId selectedNodeId =
        selectHeatSolveNode(context.executionState.state, context.executionState);
    if (!selectedNodeId.isValid()) {
        solverController->setHeatSolveContactPairs({}, false);
        if (solverController->deactivateHeatSolveIfActive()) {
            return true;
        }
        return executed;
    }

    if (selectedNodeId != context.node.id) {
        return executed;
    }

    HeatSolveParams solveParams{};
    solveParams.cellSize = static_cast<float>(getFloatParamValue(
        context.node,
        nodegraphparams::heatsolve::CellSize,
        solveParams.cellSize));
    solveParams.voxelResolution = getIntParamValue(
        context.node,
        nodegraphparams::heatsolve::VoxelResolution,
        solveParams.voxelResolution);
    if (solveParams.cellSize <= 0.0f) {
        solveParams.cellSize = HeatSolveParams{}.cellSize;
    }
    if (solveParams.voxelResolution <= 0) {
        solveParams.voxelResolution = HeatSolveParams{}.voxelResolution;
    }
    solverController->setHeatSolveParams(solveParams);

    std::vector<NodeSolverController::HeatSolveContactInput> contactPairInputs;
    bool forceContactRebuild = false;
    std::vector<GeometryData> receiverMaterialGeometryInputs;
    const std::vector<HeatMaterialBindingEntry> materialBindings = parseMaterialBindings(
        getStringParamValue(context.node, nodegraphparams::heatsolve::MaterialBindings));
    const std::unordered_map<uint32_t, HeatContactParams> contactBindings = parseContactBindings(
        getStringParamValue(context.node, nodegraphparams::heatsolve::ContactBindings));
    std::unordered_set<uint32_t> seenReceiverMaterialModelIds;
    for (const NodeGraphSocket& inputSocket : context.node.inputs) {
        const NodeDataBlock* inputValue = resolveInputValueForSocket(
            context.node,
            inputSocket.id,
            context.executionState);
        if (!inputValue) {
            continue;
        }

        if (inputValue->dataType != NodeDataType::ContactPair || !inputValue->contactPairData.hasValidContact) {
            continue;
        }

        NodeSolverController::HeatSolveContactInput contactInput{};
        contactInput.inputSocketId = inputSocket.id;
        contactInput.contactPair = inputValue->contactPairData;
        const auto bindingIt = contactBindings.find(inputSocket.id.value);
        if (bindingIt != contactBindings.end()) {
            contactInput.params = bindingIt->second;
        }
        contactPairInputs.push_back(contactInput);
        forceContactRebuild = forceContactRebuild || inputValue->contactPairData.computeRequested;

        const ContactPairData& contactPair = inputValue->contactPairData;
        auto pushReceiverMaterialModel = [&](ContactPairRole role, uint32_t modelId, const GeometryData& geometry) {
            if (role != ContactPairRole::Receiver || modelId == 0) {
                return;
            }

            if (seenReceiverMaterialModelIds.insert(modelId).second) {
                receiverMaterialGeometryInputs.push_back(geometry);
            }
        };

        pushReceiverMaterialModel(contactPair.roleA, contactPair.modelIdA, contactPair.geometryA);
        pushReceiverMaterialModel(contactPair.roleB, contactPair.modelIdB, contactPair.geometryB);
    }

    if (contactPairInputs.empty()) {
        solverController->setHeatSolveContactPairs({}, false);
        if (solverController->deactivateHeatSolveIfActive()) {
            return true;
        }
        return executed;
    }

    solverController->setHeatSolveContactPairs(contactPairInputs, forceContactRebuild);
    solverController->setHeatSolveMaterialBindings(receiverMaterialGeometryInputs, materialBindings);

    const bool wantsPaused = resetRequested
        ? false
        : getBoolParamValue(context.node, nodegraphparams::heatsolve::Paused, false);
    if (solverController->ensureHeatSolveRunningState(wantsPaused)) {
        return true;
    }

    return executed;
}

uint64_t NodeHeatSolve::makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

bool NodeHeatSolve::getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue) {
    bool value = defaultValue;
    if (tryGetNodeParamBool(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

bool NodeHeatSolve::setBoolParameter(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value) {
    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Bool;
    parameter.boolValue = value;
    return bridge.setNodeParameter(nodeId, parameter);
}

std::string NodeHeatSolve::getStringParamValue(const NodeGraphNode& node, uint32_t parameterId) {
    std::string value;
    if (tryGetNodeParamString(node, parameterId, value)) {
        return value;
    }
    return {};
}

const NodeDataBlock* NodeHeatSolve::resolveInputValueForSocket(
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

NodeGraphNodeId NodeHeatSolve::selectHeatSolveNode(
    const NodeGraphState& state,
    const NodeGraphKernelExecutionState& executionState) {
    NodeGraphNodeId selectedNodeId{};
    for (const NodeGraphNode& node : state.nodes) {
        if (canonicalNodeTypeId(node.typeId) != nodegraphtypes::HeatSolve) {
            continue;
        }

        bool enabled = false;
        if (!tryGetNodeParamBool(node, nodegraphparams::heatsolve::Enabled, enabled) || !enabled) {
            continue;
        }

        if (!NodeGraphExecutionPlanner::nodeHasAllRequiredInputs(state, node.id)) {
            continue;
        }

        bool hasContactPair = false;
        for (const NodeGraphSocket& inputSocket : node.inputs) {
            const NodeDataBlock* inputValue = resolveInputValueForSocket(node, inputSocket.id, executionState);
            if (!inputValue) {
                continue;
            }

            if (inputValue->dataType == NodeDataType::ContactPair &&
                inputValue->contactPairData.hasValidContact) {
                hasContactPair = true;
            }
        }

        if (!hasContactPair) {
            continue;
        }

        if (selectedNodeId.isValid()) {
            return {};
        }

        selectedNodeId = node.id;
    }

    return selectedNodeId;
}
