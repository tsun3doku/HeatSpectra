#include "NodeGraphCompiler.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphSceneUtils.hpp"

#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

std::unordered_set<uint64_t> NodeGraphCompiler::buildConnectedInputSocketSet(const NodeGraphState& state) {
    std::unordered_set<uint64_t> connectedInputSockets;
    connectedInputSockets.reserve(state.edges.size() * 2);
    for (const NodeGraphEdge& edge : state.edges) {
        connectedInputSockets.insert(makeSocketKey(edge.toNode, edge.toSocket));
    }

    return connectedInputSockets;
}

std::vector<std::string> NodeGraphCompiler::findMissingInputSocketNames(
    const NodeGraphNode& node,
    const std::unordered_set<uint64_t>& connectedInputSockets) {
    if (getNodeTypeId(node.typeId) == nodegraphtypes::HeatSolve) {
        bool hasVoronoiConnection = false;
        bool hasContactConnection = false;
        for (const NodeGraphSocket& inputSocket : node.inputs) {
            if (connectedInputSockets.find(makeSocketKey(node.id, inputSocket.id)) == connectedInputSockets.end()) {
                continue;
            }

            if (inputSocket.valueType == NodeGraphValueType::Volume) {
                hasVoronoiConnection = true;
            }
            if (inputSocket.valueType == NodeGraphValueType::Field) {
                hasContactConnection = true;
            }
        }

        std::vector<std::string> missing;
        if (!hasVoronoiConnection) {
            missing.push_back("Volume");
        }
        if (!hasContactConnection) {
            missing.push_back("Field");
        }
        return missing;
    }

    std::vector<std::string> missingSocketNames;
    for (const NodeGraphSocket& inputSocket : node.inputs) {
        if (connectedInputSockets.find(makeSocketKey(node.id, inputSocket.id)) != connectedInputSockets.end()) {
            continue;
        }

        missingSocketNames.push_back(inputSocket.name.empty() ? std::string("Unnamed Input") : inputSocket.name);
    }

    return missingSocketNames;
}

std::string NodeGraphCompiler::formatMissingInputsText(const std::vector<std::string>& socketNames) {
    if (socketNames.empty()) {
        return std::string();
    }

    std::ostringstream ss;
    for (std::size_t index = 0; index < socketNames.size(); ++index) {
        if (index > 0) {
            ss << ", ";
        }
        ss << socketNames[index];
    }

    return ss.str();
}

std::string NodeGraphCompiler::makeInvariantReason(const char* code, const std::string& detail) {
    std::string reason = "Invariant[";
    reason += code;
    reason += "] violated";
    if (!detail.empty()) {
        reason += ": ";
        reason += detail;
    }
    reason += ".";
    return reason;
}

bool NodeGraphCompiler::nodeHasAllRequiredInputs(const NodeGraphState& state, NodeGraphNodeId nodeId) {
    const NodeGraphNode* node = nodegraphsceneutils::findStateNodeById(state, nodeId);
    if (!node) {
        return false;
    }

    const std::unordered_set<uint64_t> connectedInputSockets = buildConnectedInputSocketSet(state);
    if (getNodeTypeId(node->typeId) == nodegraphtypes::HeatSolve) {
        bool hasVoronoiConnection = false;
        bool hasContactConnection = false;
        for (const NodeGraphSocket& inputSocket : node->inputs) {
            if (connectedInputSockets.find(makeSocketKey(node->id, inputSocket.id)) == connectedInputSockets.end()) {
                continue;
            }

            if (inputSocket.valueType == NodeGraphValueType::Volume) {
                hasVoronoiConnection = true;
            }
            if (inputSocket.valueType == NodeGraphValueType::Field) {
                hasContactConnection = true;
            }
        }

        return hasVoronoiConnection && hasContactConnection;
    }

    for (const NodeGraphSocket& inputSocket : node->inputs) {
        if (connectedInputSockets.find(makeSocketKey(node->id, inputSocket.id)) == connectedInputSockets.end()) {
            return false;
        }
    }

    return true;
}

bool NodeGraphCompiler::buildTopologicalOrder(
    const NodeGraphState& state,
    std::vector<NodeGraphNodeId>& outOrder,
    std::string* outError) {
    outOrder.clear();
    if (outError) {
        outError->clear();
    }

    if (state.nodes.empty()) {
        return true;
    }

    std::unordered_map<uint32_t, std::size_t> nodeIndexById;
    nodeIndexById.reserve(state.nodes.size() * 2);
    for (std::size_t index = 0; index < state.nodes.size(); ++index) {
        nodeIndexById[state.nodes[index].id.value] = index;
    }

    std::vector<std::vector<uint32_t>> adjacency(state.nodes.size());
    std::vector<uint32_t> inDegree(state.nodes.size(), 0);
    for (const NodeGraphEdge& edge : state.edges) {
        const auto fromIt = nodeIndexById.find(edge.fromNode.value);
        const auto toIt = nodeIndexById.find(edge.toNode.value);
        if (fromIt == nodeIndexById.end() || toIt == nodeIndexById.end()) {
            continue;
        }

        adjacency[fromIt->second].push_back(edge.toNode.value);
        ++inDegree[toIt->second];
    }

    std::vector<uint32_t> readyNodeIds;
    readyNodeIds.reserve(state.nodes.size());
    for (const NodeGraphNode& node : state.nodes) {
        const auto nodeIt = nodeIndexById.find(node.id.value);
        if (nodeIt == nodeIndexById.end()) {
            continue;
        }
        if (inDegree[nodeIt->second] == 0) {
            readyNodeIds.push_back(node.id.value);
        }
    }

    outOrder.reserve(state.nodes.size());
    std::size_t cursor = 0;
    while (cursor < readyNodeIds.size()) {
        const uint32_t currentNodeId = readyNodeIds[cursor++];
        outOrder.push_back(NodeGraphNodeId{currentNodeId});

        const auto currentIt = nodeIndexById.find(currentNodeId);
        if (currentIt == nodeIndexById.end()) {
            continue;
        }

        for (const uint32_t downstreamNodeId : adjacency[currentIt->second]) {
            const auto downstreamIt = nodeIndexById.find(downstreamNodeId);
            if (downstreamIt == nodeIndexById.end()) {
                continue;
            }

            uint32_t& downstreamDegree = inDegree[downstreamIt->second];
            if (downstreamDegree == 0) {
                continue;
            }

            --downstreamDegree;
            if (downstreamDegree == 0) {
                readyNodeIds.push_back(downstreamNodeId);
            }
        }
    }

    if (outOrder.size() != state.nodes.size()) {
        outOrder.clear();
        if (outError) {
            *outError = "Graph contains a cycle or invalid dependency chain.";
        }
        return false;
    }

    return true;
}

NodeGraphCompiled NodeGraphCompiler::compile(const NodeGraphState& state) {
    NodeGraphCompiled compiled{};
    compiled.revision = state.revision;

    std::string topoError;
    if (!buildTopologicalOrder(state, compiled.executionOrder, &topoError)) {
        compiled.isValid = false;
        if (!topoError.empty()) {
            compiled.compilationErrors.push_back(topoError);
        }
        return compiled;
    }

    const std::unordered_set<uint64_t> connectedInputSockets = buildConnectedInputSocketSet(state);

    std::vector<const NodeGraphNode*> heatSolveNodes;
    heatSolveNodes.reserve(state.nodes.size());
    for (const NodeGraphNode& node : state.nodes) {
        if (getNodeTypeId(node.typeId) == nodegraphtypes::HeatSolve) {
            heatSolveNodes.push_back(&node);
        }
    }

    if (heatSolveNodes.empty()) {
        compiled.isValid = false;
        compiled.compilationErrors.push_back(makeInvariantReason(
            "HS001",
            "At least one Heat Solve node must exist"));
        return compiled;
    }

    std::vector<const NodeGraphNode*> enabledHeatSolveNodes;
    enabledHeatSolveNodes.reserve(heatSolveNodes.size());
    for (const NodeGraphNode* node : heatSolveNodes) {
        if (!node) {
            continue;
        }

        bool enabled = false;
        if (tryGetNodeParamBool(*node, nodegraphparams::heatsolve::Enabled, enabled) && enabled) {
            enabledHeatSolveNodes.push_back(node);
        }
    }

    if (enabledHeatSolveNodes.empty()) {
        compiled.isValid = false;
        compiled.compilationErrors.push_back(makeInvariantReason(
            "HS002",
            "Exactly one enabled Heat Solve context is required"));
        return compiled;
    }

    const NodeGraphNode* selectedNode = nullptr;
    const NodeGraphNode* missingConnectionsNode = nullptr;
    for (const NodeGraphNode* node : enabledHeatSolveNodes) {
        if (!node) {
            continue;
        }

        if (!nodeHasAllRequiredInputs(state, node->id)) {
            if (!missingConnectionsNode) {
                missingConnectionsNode = node;
            }
            continue;
        }

        if (selectedNode) {
            compiled.isValid = false;
            compiled.compilationErrors.push_back(makeInvariantReason(
                "HS003",
                "Multiple enabled Heat Solve nodes resolve complete contexts; enable only one branch"));
            return compiled;
        }

        selectedNode = node;
    }

    if (selectedNode) {
        compiled.isValid = true;
        return compiled;
    }

    if (missingConnectionsNode) {
        const std::vector<std::string> missingSockets = findMissingInputSocketNames(*missingConnectionsNode, connectedInputSockets);
        const std::string missingInputsText = formatMissingInputsText(missingSockets);
        std::string detail =
            "Enabled Heat Solve node '" + displayNodeLabel(*missingConnectionsNode) +
            "' is missing required input socket connections";
        if (!missingInputsText.empty()) {
            detail += "; missing: " + missingInputsText;
        }
        compiled.compilationErrors.push_back(makeInvariantReason("HS005", detail));
    } else {
        compiled.compilationErrors.push_back(makeInvariantReason(
            "HS005",
            "Enabled Heat Solve node is missing required input socket connections"));
    }

    compiled.isValid = false;
    return compiled;
}
