#include "NodeHeatSolve.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphExecutionPlanner.hpp"
#include "NodeSolverController.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

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
        if (solverController->deactivateHeatSolveIfActive()) {
            return true;
        }
        return executed;
    }

    if (selectedNodeId != context.node.id) {
        return executed;
    }

    std::vector<uint32_t> sourceGraphModelIds;
    std::vector<uint32_t> receiverGraphModelIds;
    std::unordered_set<uint32_t> seenSourceModelIds;
    std::unordered_set<uint32_t> seenReceiverModelIds;
    for (const NodeGraphSocket& inputSocket : context.node.inputs) {
        const NodeDataBlock* inputValue = resolveInputValueForSocket(
            context.node,
            inputSocket.id,
            context.executionState);
        if (!inputValue || inputValue->geometry.modelId == 0) {
            continue;
        }

        if (inputValue->dataType == NodeDataType::HeatSource) {
            if (seenSourceModelIds.insert(inputValue->geometry.modelId).second) {
                sourceGraphModelIds.push_back(inputValue->geometry.modelId);
            }
        } else if (inputValue->dataType == NodeDataType::HeatReceiver) {
            if (seenReceiverModelIds.insert(inputValue->geometry.modelId).second) {
                receiverGraphModelIds.push_back(inputValue->geometry.modelId);
            }
        }
    }
    solverController->setHeatSolveModelRoles(sourceGraphModelIds, receiverGraphModelIds);

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

        bool hasReceiver = false;
        bool hasSource = false;
        for (const NodeGraphSocket& inputSocket : node.inputs) {
            const NodeDataBlock* inputValue = resolveInputValueForSocket(node, inputSocket.id, executionState);
            if (!inputValue) {
                continue;
            }

            if (inputValue->dataType == NodeDataType::HeatReceiver) {
                hasReceiver = true;
            } else if (inputValue->dataType == NodeDataType::HeatSource) {
                hasSource = true;
            }
        }

        if (!hasReceiver || !hasSource) {
            continue;
        }

        if (selectedNodeId.isValid()) {
            return {};
        }

        selectedNodeId = node.id;
    }

    return selectedNodeId;
}
