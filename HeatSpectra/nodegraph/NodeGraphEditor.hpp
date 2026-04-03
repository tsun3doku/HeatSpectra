#pragma once

#include "NodeGraphTypes.hpp"

#include <glm/vec3.hpp>

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

    bool ensureTransformForModelNode(NodeGraphNodeId modelNodeId, NodeGraphNodeId& outTransformNodeId);
    bool readTransformNodeValues(
        NodeGraphNodeId nodeId,
        glm::vec3& outTranslation,
        glm::vec3& outRotationDegrees) const;
    bool writeTransformTranslation(NodeGraphNodeId nodeId, const glm::vec3& translation);
    bool writeTransformRotation(NodeGraphNodeId nodeId, const glm::vec3& rotationDegrees);

private:
    NodeGraphBridge* bridge = nullptr;
};
