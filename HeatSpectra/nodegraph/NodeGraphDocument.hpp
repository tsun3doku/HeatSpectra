#pragma once

#include "NodeGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

class NodeGraphDocument {
public:
    NodeGraphDocument();

    NodeGraphNodeId addNode(const NodeTypeId& typeId, const std::string& title, float x, float y);
    bool removeNode(NodeGraphNodeId nodeId);
    bool moveNode(NodeGraphNodeId nodeId, float x, float y);
    bool setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter);
    bool appendSocket(NodeGraphNodeId nodeId, const NodeSocketSignature& socketSignature, NodeGraphSocketId* outSocketId = nullptr);

    bool addConnection(
        NodeGraphNodeId fromNode,
        NodeGraphSocketId fromSocket,
        NodeGraphNodeId toNode,
        NodeGraphSocketId toSocket,
        NodeGraphEdgeId* outEdgeId = nullptr);
    bool removeConnection(NodeGraphEdgeId edgeId);

    void clear();

    const std::vector<NodeGraphNode>& getNodes() const {
        return nodes;
    }

    const std::vector<NodeGraphEdge>& getEdges() const {
        return edges;
    }

    const NodeGraphNode* findNode(NodeGraphNodeId nodeId) const;
    NodeGraphNode* findNode(NodeGraphNodeId nodeId);

    const NodeGraphSocket* findInputSocket(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) const;
    const NodeGraphSocket* findOutputSocket(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) const;

    uint64_t getRevision() const {
        return revision;
    }

private:
    NodeGraphSocketId allocateSocketId();
    std::vector<NodeGraphSocket> buildSocketsFromInterface(const NodeTypeDefinition& definition, NodeGraphSocketDirection direction);
    void bumpRevision();

    uint32_t nextNodeId = 1;
    uint32_t nextSocketId = 1;
    uint32_t nextEdgeId = 1;
    uint64_t revision = 1;

    std::vector<NodeGraphNode> nodes;
    std::vector<NodeGraphEdge> edges;
};
