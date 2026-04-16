#include "NodeGraphDebugCache.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphPayloadTypes.hpp"
#include "NodePayloadRegistry.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace {

NodeGraphDebugCache gCache;

const GeometryData* resolveGeometryForDebugBlock(const NodeDataBlock& block, const NodePayloadRegistry* payloadRegistry) {
    return resolveGeometryForDataBlock(block, payloadRegistry);
}

const char* attributeDomainName(GeometryAttributeDomain domain) {
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

const char* attributeDataTypeName(GeometryAttributeDataType dataType) {
    switch (dataType) {
    case GeometryAttributeDataType::Int:
        return "int";
    case GeometryAttributeDataType::Bool:
        return "bool";
    case GeometryAttributeDataType::Float:
    default:
        return "float";
    }
}

std::size_t attributeElementCount(const GeometryData& geometry, GeometryAttributeDomain domain) {
    switch (domain) {
    case GeometryAttributeDomain::Point:
        return geometry.pointPositions.size() / 3;
    case GeometryAttributeDomain::Primitive:
        return geometry.triangleIndices.size() / 3;
    case GeometryAttributeDomain::Vertex:
        return geometry.triangleIndices.size();
    case GeometryAttributeDomain::Detail:
    default:
        return 1;
    }
}

template <typename TValue>
void appendTupleSample(
    std::ostringstream& stream,
    const std::vector<TValue>& values,
    std::size_t tupleStart,
    uint32_t tupleSize) {
    if (tupleSize <= 1) {
        if (tupleStart < values.size()) {
            stream << values[tupleStart];
        }
        return;
    }

    stream << "(";
    for (uint32_t i = 0; i < tupleSize; ++i) {
        const std::size_t index = tupleStart + static_cast<std::size_t>(i);
        if (index >= values.size()) {
            break;
        }
        if (i > 0) {
            stream << ", ";
        }
        stream << values[index];
    }
    stream << ")";
}

void appendTupleSampleBool(
    std::ostringstream& stream,
    const std::vector<uint8_t>& values,
    std::size_t tupleStart,
    uint32_t tupleSize) {
    if (tupleSize <= 1) {
        if (tupleStart < values.size()) {
            stream << (values[tupleStart] ? "true" : "false");
        }
        return;
    }

    stream << "(";
    for (uint32_t i = 0; i < tupleSize; ++i) {
        const std::size_t index = tupleStart + static_cast<std::size_t>(i);
        if (index >= values.size()) {
            break;
        }
        if (i > 0) {
            stream << ", ";
        }
        stream << (values[index] ? "true" : "false");
    }
    stream << ")";
}

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

    if (!payloadRegistry || block->payloadHandle.key == 0) {
        return info;
    }

    if (block->dataType == NodePayloadType::Geometry ||
        block->dataType == NodePayloadType::Remesh ||
        block->dataType == NodePayloadType::HeatReceiver ||
        block->dataType == NodePayloadType::HeatSource) {
        const GeometryData* geometry = resolveGeometryForDebugBlock(*block, payloadRegistry);
        if (!geometry) {
            return info;
        }

        constexpr std::size_t kMaxSampleElements = 12;
        info.attributes.reserve(geometry->attributes.size());
        for (const GeometryAttribute& attribute : geometry->attributes) {
            NodeGraphRuntimeAttributeDebugInfo attributeInfo{};
            attributeInfo.name = attribute.name;
            attributeInfo.domain = attributeDomainName(attribute.domain);
            attributeInfo.dataType = attributeDataTypeName(attribute.dataType);
            attributeInfo.tupleSize = attribute.tupleSize == 0 ? 1 : attribute.tupleSize;

            const std::size_t expectedElements = attributeElementCount(*geometry, attribute.domain);
            std::size_t availableElements = 0;
            switch (attribute.dataType) {
            case GeometryAttributeDataType::Int:
                availableElements = attribute.tupleSize == 0
                    ? 0
                    : attribute.intValues.size() / attributeInfo.tupleSize;
                break;
            case GeometryAttributeDataType::Bool:
                availableElements = attribute.tupleSize == 0
                    ? 0
                    : attribute.boolValues.size() / attributeInfo.tupleSize;
                break;
            case GeometryAttributeDataType::Float:
            default:
                availableElements = attribute.tupleSize == 0
                    ? 0
                    : attribute.floatValues.size() / attributeInfo.tupleSize;
                break;
            }

            const std::size_t elementCount = std::min(expectedElements, availableElements);
            attributeInfo.elementCount = static_cast<uint32_t>(elementCount);
            const std::size_t sampleCount = std::min(elementCount, kMaxSampleElements);
            attributeInfo.sampleValues.reserve(sampleCount);

            for (std::size_t elementIndex = 0; elementIndex < sampleCount; ++elementIndex) {
                const std::size_t tupleStart = elementIndex * attributeInfo.tupleSize;
                std::ostringstream stream;
                stream << std::fixed << std::setprecision(4);
                switch (attribute.dataType) {
                case GeometryAttributeDataType::Int:
                    appendTupleSample(stream, attribute.intValues, tupleStart, attributeInfo.tupleSize);
                    break;
                case GeometryAttributeDataType::Bool:
                    appendTupleSampleBool(stream, attribute.boolValues, tupleStart, attributeInfo.tupleSize);
                    break;
                case GeometryAttributeDataType::Float:
                default:
                    appendTupleSample(stream, attribute.floatValues, tupleStart, attributeInfo.tupleSize);
                    break;
                }
                attributeInfo.sampleValues.push_back(stream.str());
            }

            info.attributes.push_back(attributeInfo);
        }
    }

    return info;
}
