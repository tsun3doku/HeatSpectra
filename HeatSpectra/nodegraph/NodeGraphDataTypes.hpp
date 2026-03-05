#pragma once

#include "NodeGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GeometryAttribute {
    std::string name;
    GeometryAttributeDomain domain = GeometryAttributeDomain::Point;
    GeometryAttributeDataType dataType = GeometryAttributeDataType::Float;
    uint32_t tupleSize = 1;
    std::vector<float> floatValues;
    std::vector<int64_t> intValues;
    std::vector<uint8_t> boolValues;
};

struct GeometryGroup {
    uint32_t id = 0;
    std::string name;
    std::string source;
};

struct GeometryData {
    std::string sourceModelPath;
    uint32_t modelId = 0;
    std::vector<float> pointPositions;
    std::vector<uint32_t> triangleIndices;
    std::vector<uint32_t> triangleGroupIds;
    std::vector<GeometryGroup> groups;
    std::vector<GeometryAttribute> attributes;
};

struct NodeDataBlock {
    NodeDataType dataType = NodeDataType::None;
    GeometryData geometry{};
    double scalarFloatValue = 0.0;
    int64_t scalarIntValue = 0;
    bool scalarBoolValue = false;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<NodeGraphNodeId> lineageNodeIds;
};

const char* nodeDataTypeToString(NodeDataType dataType);
void setDetailBoolAttribute(GeometryData& geometry, const std::string& name, bool value);
void setPointFloatAttributeConstant(GeometryData& geometry, const std::string& name, float value);
void setPrimitiveIntAttributeFromUInt32(
    GeometryData& geometry,
    const std::string& name,
    const std::vector<uint32_t>& values);
void ensureGeometryGroups(GeometryData& geometry);
void refreshNodeDataBlockMetadata(NodeDataBlock& dataBlock);
void seedOutputDataBlocksFromInputs(
    const NodeGraphNode& node,
    const std::vector<const NodeDataBlock*>& inputs,
    std::vector<NodeDataBlock>& outputs);
