#pragma once

#include "NodeGraphCoreTypes.hpp"
#include "NodeGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GeometryData;
class NodePayloadRegistry;

struct NodeDataBlock {
    NodePayloadType dataType = NodePayloadType::None;
    NodeDataHandle payloadHandle{};
    double scalarFloatValue = 0.0;
    int64_t scalarIntValue = 0;
    bool scalarBoolValue = false;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<NodeGraphNodeId> lineageNodeIds;
};

const char* nodePayloadTypeName(NodePayloadType payloadType);
const GeometryData* resolveGeometryForDataBlock(const NodeDataBlock& dataBlock, const NodePayloadRegistry* registry);
void setGeometryPrimitiveIntAttribute(GeometryData& geometry, const std::string& name, const std::vector<uint32_t>& values);
void normalizeGeometryGroups(GeometryData& geometry);
void updatePayloadHash(GeometryData& geometry);
uint64_t payloadHashForDataBlock(const NodeDataBlock& dataBlock, const NodePayloadRegistry* registry);
void updateDataBlockMetadata(NodeDataBlock& dataBlock, const NodePayloadRegistry* registry = nullptr);
void initializeOutputsFromInputs(const NodeGraphNode& node, const std::vector<const NodeDataBlock*>& inputs, std::vector<NodeDataBlock>& outputs);
