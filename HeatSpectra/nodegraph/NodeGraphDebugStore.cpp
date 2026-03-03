#include "NodeGraphDebugStore.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace {

NodeGraphDebugStore gStore;
constexpr std::size_t maxDebugAttributeSamples = 128;

const char* domainName(GeometryAttributeDomain domain) {
    switch (domain) {
    case GeometryAttributeDomain::Point:
        return "point";
    case GeometryAttributeDomain::Primitive:
        return "primitive";
    case GeometryAttributeDomain::Vertex:
        return "vertex";
    case GeometryAttributeDomain::Detail:
        return "detail";
    default:
        return "unknown";
    }
}

const char* dataTypeName(GeometryAttributeDataType dataType) {
    switch (dataType) {
    case GeometryAttributeDataType::Float:
        return "float";
    case GeometryAttributeDataType::Int:
        return "int";
    case GeometryAttributeDataType::Bool:
        return "bool";
    default:
        return "unknown";
    }
}

std::size_t elemCount(const GeometryAttribute& attribute) {
    const std::size_t tuple = std::max<std::size_t>(1, attribute.tupleSize);
    switch (attribute.dataType) {
    case GeometryAttributeDataType::Float:
        return attribute.floatValues.size() / tuple;
    case GeometryAttributeDataType::Int:
        return attribute.intValues.size() / tuple;
    case GeometryAttributeDataType::Bool:
        return attribute.boolValues.size() / tuple;
    default:
        return 0;
    }
}

std::string elemValue(const GeometryAttribute& attribute, std::size_t elemIndex) {
    const std::size_t tuple = std::max<std::size_t>(1, attribute.tupleSize);
    const std::size_t start = elemIndex * tuple;

    std::ostringstream stream;
    for (std::size_t c = 0; c < tuple; ++c) {
        if (c > 0) {
            stream << ", ";
        }

        const std::size_t valueIndex = start + c;
        switch (attribute.dataType) {
        case GeometryAttributeDataType::Float:
            if (valueIndex < attribute.floatValues.size()) {
                stream << std::fixed << std::setprecision(4) << attribute.floatValues[valueIndex];
            } else {
                stream << "0.0";
            }
            break;
        case GeometryAttributeDataType::Int:
            if (valueIndex < attribute.intValues.size()) {
                stream << attribute.intValues[valueIndex];
            } else {
                stream << "0";
            }
            break;
        case GeometryAttributeDataType::Bool:
            if (valueIndex < attribute.boolValues.size()) {
                stream << (attribute.boolValues[valueIndex] != 0 ? "true" : "false");
            } else {
                stream << "false";
            }
            break;
        default:
            stream << "n/a";
            break;
        }
    }

    return stream.str();
}

}

NodeGraphDebugStore& NodeGraphDebugStore::instance() {
    return gStore;
}

bool NodeGraphDebugStore::tryGetLatestNodeDebugInfo(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo) {
    return instance().tryGetNode(nodeId, outInfo);
}

void NodeGraphDebugStore::setState(const NodeGraphState& graphState) {
    std::lock_guard<std::mutex> lock(mutex);
    state = graphState;
}

void NodeGraphDebugStore::publish(
    uint64_t runtimeRevision,
    std::unordered_map<uint64_t, uint64_t>&& inputSources,
    std::unordered_map<uint64_t, NodeDataBlock>&& outputs) {
    std::lock_guard<std::mutex> lock(mutex);
    revision = runtimeRevision;
    srcByInput = std::move(inputSources);
    outBySocket = std::move(outputs);
}

bool NodeGraphDebugStore::tryGetNode(NodeGraphNodeId nodeId, NodeGraphRuntimeNodeDebugInfo& outInfo) const {
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
    outInfo.nodeTypeId = canonicalNodeTypeId(nodeIt->typeId);

    outInfo.inputs.reserve(nodeIt->inputs.size());
    for (const NodeGraphSocket& socket : nodeIt->inputs) {
        const NodeDataBlock* block = nullptr;
        const uint64_t inputKey = socketKey(nodeIt->id, socket.id);
        const auto srcIt = srcByInput.find(inputKey);
        if (srcIt != srcByInput.end()) {
            const auto dataIt = outBySocket.find(srcIt->second);
            if (dataIt != outBySocket.end()) {
                block = &dataIt->second;
            }
        }

        outInfo.inputs.push_back(socketInfo(socket, block));
    }

    outInfo.outputs.reserve(nodeIt->outputs.size());
    for (const NodeGraphSocket& socket : nodeIt->outputs) {
        const NodeDataBlock* block = nullptr;
        const uint64_t outputKey = socketKey(nodeIt->id, socket.id);
        const auto outIt = outBySocket.find(outputKey);
        if (outIt != outBySocket.end()) {
            block = &outIt->second;
        }

        outInfo.outputs.push_back(socketInfo(socket, block));
    }

    return true;
}

uint64_t NodeGraphDebugStore::socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

NodeGraphRuntimeSocketDebugInfo NodeGraphDebugStore::socketInfo(const NodeGraphSocket& socket, const NodeDataBlock* block) {
    NodeGraphRuntimeSocketDebugInfo info{};
    info.socketId = socket.id;
    info.socketName = socket.name;
    info.direction = socket.direction;
    if (!block) {
        return info;
    }

    info.hasValue = true;
    info.dataType = nodeDataTypeToString(block->dataType);
    info.metadata = block->metadata;
    info.lineageNodeIds = block->lineageNodeIds;

    if (block->dataType == NodeDataType::Geometry ||
        block->dataType == NodeDataType::HeatReceiver ||
        block->dataType == NodeDataType::HeatSource) {
        info.attributes.reserve(block->geometry.attributes.size());
        for (const GeometryAttribute& attribute : block->geometry.attributes) {
            NodeGraphRuntimeAttributeDebugInfo attr{};
            attr.name = attribute.name;
            attr.domain = domainName(attribute.domain);
            attr.dataType = dataTypeName(attribute.dataType);
            attr.tupleSize = attribute.tupleSize;
            const std::size_t count = elemCount(attribute);
            attr.elementCount = static_cast<uint32_t>(count);
            const std::size_t sampleCount = std::min(count, maxDebugAttributeSamples);
            attr.sampleValues.reserve(sampleCount);
            for (std::size_t i = 0; i < sampleCount; ++i) {
                attr.sampleValues.push_back(elemValue(attribute, i));
            }
            info.attributes.push_back(std::move(attr));
        }
    }

    return info;
}
