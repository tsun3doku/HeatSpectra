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
    NodeDataType dataType = NodeDataType::None;
    NodeDataHandle payloadHandle{};
    double scalarFloatValue = 0.0;
    int64_t scalarIntValue = 0;
    bool scalarBoolValue = false;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<NodeGraphNodeId> lineageNodeIds;
};

const char* nodeDataTypeName(NodeDataType dataType);
void setGeometryDetailBool(GeometryData& geometry, const std::string& name, bool value);
void setGeometryPrimitiveIntAttribute(GeometryData& geometry, const std::string& name, const std::vector<uint32_t>& values);
void normalizeGeometryGroups(GeometryData& geometry);
void bumpGeometryRevision(GeometryData& geometry);
void updateDataBlockMetadata(NodeDataBlock& dataBlock, const NodePayloadRegistry* registry = nullptr);
void initializeOutputsFromInputs(const NodeGraphNode& node, const std::vector<const NodeDataBlock*>& inputs, std::vector<NodeDataBlock>& outputs);
