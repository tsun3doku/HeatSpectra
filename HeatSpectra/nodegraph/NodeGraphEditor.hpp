#pragma once

#include "NodeGraphTypes.hpp"

#include <functional>
#include <string>
#include <vector>

class NodeGraphBridge;

class NodeGraphEditor {
public:
    struct CopiedNode {
        NodeGraphNodeId sourceNodeId{};
        NodeTypeId typeId;
        std::string title;
        float x = 0.0f;
        float y = 0.0f;
        std::vector<NodeGraphParamValue> parameters;
        std::vector<NodeGraphSocketId> outputSocketIds;
    };

    struct CopiedEdge {
        NodeGraphNodeId fromNode{};
        NodeGraphSocketId fromSocket{};
        NodeGraphNodeId toNode{};
        NodeGraphSocketId toSocket{};
    };

    NodeGraphEditor() = default;
    explicit NodeGraphEditor(NodeGraphBridge* bridge);
    explicit NodeGraphEditor(NodeGraphBridge& bridge);

    void setBridge(NodeGraphBridge* bridgePtr);
    void resetToDefaultGraph();

    NodeGraphNodeId addNode(const NodeTypeId& typeId, const std::string& title, float x, float y);
    bool removeNode(NodeGraphNodeId nodeId);
    bool moveNode(NodeGraphNodeId nodeId, float x, float y);
    bool setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter);
    bool updateNodeParameter(NodeGraphNodeId nodeId, uint32_t paramId, const std::function<bool(NodeGraphParamValue&)>& updater);
    bool connectSockets(
        NodeGraphNodeId fromNode,
        NodeGraphSocketId fromSocket,
        NodeGraphNodeId toNode,
        NodeGraphSocketId toSocket,
        std::string& errorMessage,
        bool replaceExistingInput = true);
    bool removeConnection(NodeGraphEdgeId edgeId);
    bool disconnectIncomingInput(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);
    bool pasteCopiedNodes(
        const std::vector<CopiedNode>& copiedNodes,
        const std::vector<CopiedEdge>& copiedEdges,
        float positionOffset,
        std::vector<NodeGraphNodeId>& outCreatedNodeIds);

private:
    NodeGraphBridge* bridge = nullptr;
};
