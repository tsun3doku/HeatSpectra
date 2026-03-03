#pragma once

#include "NodeGraphDataTypes.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct NodeGraphRuntimeAttributeDebugInfo {
    std::string name;
    std::string domain;
    std::string dataType;
    uint32_t tupleSize = 1;
    uint32_t elementCount = 0;
    std::vector<std::string> sampleValues;
};

struct NodeGraphRuntimeSocketDebugInfo {
    NodeGraphSocketId socketId{};
    std::string socketName;
    NodeGraphSocketDirection direction = NodeGraphSocketDirection::Input;
    bool hasValue = false;
    std::string dataType = "none";
    std::unordered_map<std::string, std::string> metadata;
    std::vector<NodeGraphNodeId> lineageNodeIds;
    std::vector<NodeGraphRuntimeAttributeDebugInfo> attributes;
};

struct NodeGraphRuntimeNodeDebugInfo {
    uint64_t revision = 0;
    NodeGraphNodeId nodeId{};
    NodeTypeId nodeTypeId;
    std::vector<NodeGraphRuntimeSocketDebugInfo> inputs;
    std::vector<NodeGraphRuntimeSocketDebugInfo> outputs;
};

class NodeGraphDebugStore {
public:
    static NodeGraphDebugStore& instance();
    static bool tryGetLatestNodeDebugInfo(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo);

    void setState(const NodeGraphState& state);
    void publish(uint64_t revision, std::unordered_map<uint64_t, uint64_t>&& srcByInput, std::unordered_map<uint64_t, NodeDataBlock>&& outBySocket);
    bool tryGetNode(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo) const;

private:
    static uint64_t socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);
    static NodeGraphRuntimeSocketDebugInfo socketInfo(const NodeGraphSocket& socket, const NodeDataBlock* block);

    mutable std::mutex mutex;
    NodeGraphState state{};
    uint64_t revision = 0;
    std::unordered_map<uint64_t, uint64_t> srcByInput;
    std::unordered_map<uint64_t, NodeDataBlock> outBySocket;
};
