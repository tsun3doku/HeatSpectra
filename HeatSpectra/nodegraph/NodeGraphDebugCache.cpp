#include "NodeGraphDebugCache.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include <algorithm>

namespace {

NodeGraphDebugCache gCache;

} // namespace

NodeGraphDebugCache& NodeGraphDebugCache::instance() {
    return gCache;
}

bool NodeGraphDebugCache::tryGetLatestNodeDebugInfo(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo) {
    return instance().tryGetNode(nodeId, outInfo);
}

void NodeGraphDebugCache::setState(const NodeGraphState& graphState, NodePayloadRegistry* registry) {
    std::lock_guard<std::mutex> lock(mutex);
    state = graphState;
    revision = graphState.revision;
    srcByInput.clear();
    outBySocket.clear();
    payloadRegistry = registry;
}

void NodeGraphDebugCache::update(
    uint64_t runtimeRevision,
    const std::unordered_map<uint64_t, uint64_t>& inputSources,
    const std::unordered_map<uint64_t, EvaluatedSocketValue>& outputs) {
    std::lock_guard<std::mutex> lock(mutex);
    revision = runtimeRevision;
    srcByInput = inputSources;
    outBySocket = outputs;
}

bool NodeGraphDebugCache::tryGetNode(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo) const {
    if (!nodeId.isValid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    const auto nodeIt = std::find_if(
        state.nodes.begin(),
        state.nodes.end(),
        [nodeId](const NodeGraphNode& candidate) {
            return candidate.id == nodeId;
        });
    if (nodeIt == state.nodes.end()) {
        return false;
    }

    outInfo = {};
    outInfo.revision = revision;
    outInfo.nodeId = nodeIt->id;
    outInfo.nodeTypeId = getNodeTypeId(nodeIt->typeId);

    outInfo.inputs.reserve(nodeIt->inputs.size());
    for (const NodeGraphSocket& socket : nodeIt->inputs) {
        const EvaluatedSocketValue* value = nullptr;
        const uint64_t inputKey = socketKey(nodeIt->id, socket.id);
        const auto srcIt = srcByInput.find(inputKey);
        if (srcIt != srcByInput.end()) {
            const auto dataIt = outBySocket.find(srcIt->second);
            if (dataIt != outBySocket.end()) {
                value = &dataIt->second;
            }
        }

        outInfo.inputs.push_back(socketInfo(socket, value));
    }

    outInfo.outputs.reserve(nodeIt->outputs.size());
    for (const NodeGraphSocket& socket : nodeIt->outputs) {
        const EvaluatedSocketValue* value = nullptr;
        const uint64_t outputKey = socketKey(nodeIt->id, socket.id);
        const auto outIt = outBySocket.find(outputKey);
        if (outIt != outBySocket.end()) {
            value = &outIt->second;
        }

        outInfo.outputs.push_back(socketInfo(socket, value));
    }

    return true;
}

uint64_t NodeGraphDebugCache::socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

NodeGraphRuntimeSocketDebugInfo NodeGraphDebugCache::socketInfo(const NodeGraphSocket& socket, const EvaluatedSocketValue* value) const {
    NodeGraphRuntimeSocketDebugInfo info{};
    info.socketId = socket.id;
    info.socketName = socket.name;
    info.direction = socket.direction;
    if (!value) {
        return info;
    }

    switch (value->status) {
    case EvaluatedSocketStatus::Value:
        info.status = "value";
        break;
    case EvaluatedSocketStatus::Error:
        info.status = "error";
        break;
    case EvaluatedSocketStatus::Missing:
    default:
        info.status = "missing";
        break;
    }
    info.error = value->error;
    if (value->status != EvaluatedSocketStatus::Value) {
        return info;
    }

    const NodeDataBlock* block = &value->data;
    info.hasValue = true;
    info.dataType = nodePayloadTypeName(block->dataType);
    info.metadata = block->metadata;
    info.lineageNodeIds = block->lineageNodeIds;

    return info;
}