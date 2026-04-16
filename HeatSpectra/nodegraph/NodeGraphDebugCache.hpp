#pragma once

#include "NodeGraphKernels.hpp"

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
    std::string status = "missing";
    bool hasValue = false;
    std::string error;
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

class NodeGraphDebugCache {
public:
    static NodeGraphDebugCache& instance();
    static bool tryGetLatestNodeDebugInfo(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo);

    void setState(const NodeGraphState& state, NodePayloadRegistry* registry);
    void update(
        uint64_t revision,
        const std::unordered_map<uint64_t, uint64_t>& srcByInput,
        const std::unordered_map<uint64_t, EvaluatedSocketValue>& outBySocket);
    bool tryGetNode(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo) const;

private:
    static uint64_t socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);
    NodeGraphRuntimeSocketDebugInfo socketInfo(const NodeGraphSocket& socket, const EvaluatedSocketValue* value) const;

    mutable std::mutex mutex;
    NodeGraphState state{};
    uint64_t revision = 0;
    std::unordered_map<uint64_t, uint64_t> srcByInput;
    std::unordered_map<uint64_t, EvaluatedSocketValue> outBySocket;
    NodePayloadRegistry* payloadRegistry = nullptr;
};
