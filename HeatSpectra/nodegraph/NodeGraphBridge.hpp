#pragma once

#include "NodeGraphDocument.hpp"
#include "NodeGraphCompiler.hpp"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

class NodeGraphBridge {
public:
    NodeGraphBridge();

    void clear();

    NodeGraphNodeId addNode(const NodeTypeId& typeId, const std::string& title, float x, float y);
    bool removeNode(NodeGraphNodeId nodeId);
    bool moveNode(NodeGraphNodeId nodeId, float x, float y);
    bool getNode(NodeGraphNodeId nodeId, NodeGraphNode& outNode) const;
    bool setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter);
    bool appendSocket(
        NodeGraphNodeId nodeId,
        const NodeSocketSignature& socketSignature,
        NodeGraphSocketId* outSocketId = nullptr);

    bool connectSockets(
        NodeGraphNodeId fromNode,
        NodeGraphSocketId fromSocket,
        NodeGraphNodeId toNode,
        NodeGraphSocketId toSocket,
        std::string& errorMessage,
        bool replaceExistingInput = true);
    bool removeConnection(NodeGraphEdgeId edgeId);

    NodeGraphCompiled compiledState() const;
    bool canExecuteHeatSolve(std::string& reason) const;

    NodeGraphState state() const;
    bool consumeChanges(uint64_t& lastSeenRevision, NodeGraphDelta& outDelta) const;

private:
    void rebuildStateLocked();
    void pushChangesLocked(const std::vector<NodeGraphChange>& changes);

    mutable std::mutex mutex;
    NodeGraphDocument document;
    NodeGraphState graphState;
    std::vector<std::pair<uint64_t, NodeGraphChange>> changeLog;
    uint64_t changeRevision = 0;
};
