#include "NodeGraphDataTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

const char* nodeDataTypeToString(NodeDataType dataType) {
    switch (dataType) {
    case NodeDataType::Geometry:
        return "geometry";
    case NodeDataType::HeatReceiver:
        return "heat_receiver";
    case NodeDataType::HeatSource:
        return "heat_source";
    case NodeDataType::ScalarFloat:
        return "scalar_float";
    case NodeDataType::ScalarInt:
        return "scalar_int";
    case NodeDataType::ScalarBool:
        return "scalar_bool";
    case NodeDataType::None:
    default:
        return "none";
    }
}

namespace {

GeometryAttribute* findAttribute(
    GeometryData& geometry,
    const std::string& name,
    GeometryAttributeDomain domain) {
    for (GeometryAttribute& attribute : geometry.attributes) {
        if (attribute.name == name && attribute.domain == domain) {
            return &attribute;
        }
    }
    return nullptr;
}

std::string geometryAttributeSummary(const GeometryData& geometry) {
    if (geometry.attributes.empty()) {
        return "none";
    }

    std::string summary;
    for (std::size_t index = 0; index < geometry.attributes.size(); ++index) {
        if (index > 0) {
            summary += ", ";
        }
        summary += geometry.attributes[index].name;
        if (index >= 7 && geometry.attributes.size() > 8) {
            summary += ", ...";
            break;
        }
    }
    return summary;
}

std::string defaultGroupName(uint32_t groupId) {
    return std::string("Group ") + std::to_string(groupId);
}

std::string geometryGroupSummary(const GeometryData& geometry) {
    if (geometry.groups.empty()) {
        return "none";
    }

    std::vector<GeometryGroup> groups = geometry.groups;
    std::sort(groups.begin(), groups.end(), [](const GeometryGroup& lhs, const GeometryGroup& rhs) {
        return lhs.id < rhs.id;
    });

    std::string summary;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (index > 0) {
            summary += ", ";
        }

        summary += std::to_string(groups[index].id);
        summary += ":";
        summary += groups[index].name.empty() ? defaultGroupName(groups[index].id) : groups[index].name;

        if (index >= 5 && groups.size() > 6) {
            summary += ", ...";
            break;
        }
    }
    return summary;
}

std::string geometryGroupNamesList(const GeometryData& geometry) {
    if (geometry.groups.empty()) {
        return std::string();
    }

    std::vector<GeometryGroup> groups = geometry.groups;
    std::sort(groups.begin(), groups.end(), [](const GeometryGroup& lhs, const GeometryGroup& rhs) {
        return lhs.id < rhs.id;
    });

    std::string names;
    for (const GeometryGroup& group : groups) {
        const std::string groupName = group.name.empty() ? defaultGroupName(group.id) : group.name;
        if (groupName.empty()) {
            continue;
        }

        if (!names.empty()) {
            names += ";";
        }
        names += groupName;
    }
    return names;
}

bool startsWithToken(const std::string& value, const char* token) {
    if (!token) {
        return false;
    }

    std::size_t tokenLength = 0;
    while (token[tokenLength] != '\0') {
        ++tokenLength;
    }
    if (value.size() < tokenLength) {
        return false;
    }

    for (std::size_t index = 0; index < tokenLength; ++index) {
        const unsigned char lhs = static_cast<unsigned char>(value[index]);
        const unsigned char rhs = static_cast<unsigned char>(token[index]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

const char* geometryGroupTypeMetadataKey(const GeometryGroup& group) {
    if (startsWithToken(group.source, "obj.object")) {
        return "geometry.group_names.object";
    }
    if (startsWithToken(group.source, "obj.material")) {
        return "geometry.group_names.material";
    }
    if (startsWithToken(group.source, "obj.smooth")) {
        return "geometry.group_names.smooth";
    }
    return "geometry.group_names.vertex";
}

std::string geometryGroupNamesListForType(const GeometryData& geometry, const char* metadataKey) {
    if (!metadataKey || geometry.groups.empty()) {
        return {};
    }

    std::vector<GeometryGroup> groups = geometry.groups;
    std::sort(groups.begin(), groups.end(), [](const GeometryGroup& lhs, const GeometryGroup& rhs) {
        return lhs.id < rhs.id;
    });

    std::unordered_set<std::string> seenNames;
    std::string names;
    for (const GeometryGroup& group : groups) {
        if (std::string(geometryGroupTypeMetadataKey(group)) != metadataKey) {
            continue;
        }

        const std::string groupName = group.name.empty() ? defaultGroupName(group.id) : group.name;
        if (groupName.empty() || !seenNames.insert(groupName).second) {
            continue;
        }

        if (!names.empty()) {
            names += ";";
        }
        names += groupName;
    }
    return names;
}

} // namespace

void setDetailBoolAttribute(GeometryData& geometry, const std::string& name, bool value) {
    GeometryAttribute* attribute = findAttribute(geometry, name, GeometryAttributeDomain::Detail);
    if (!attribute) {
        geometry.attributes.push_back({});
        attribute = &geometry.attributes.back();
    }

    attribute->name = name;
    attribute->domain = GeometryAttributeDomain::Detail;
    attribute->dataType = GeometryAttributeDataType::Bool;
    attribute->tupleSize = 1;
    attribute->floatValues.clear();
    attribute->intValues.clear();
    attribute->boolValues.assign(1, value ? 1 : 0);
}

void setPointFloatAttributeConstant(GeometryData& geometry, const std::string& name, float value) {
    GeometryAttribute* attribute = findAttribute(geometry, name, GeometryAttributeDomain::Point);
    if (!attribute) {
        geometry.attributes.push_back({});
        attribute = &geometry.attributes.back();
    }

    attribute->name = name;
    attribute->domain = GeometryAttributeDomain::Point;
    attribute->dataType = GeometryAttributeDataType::Float;
    attribute->tupleSize = 1;
    attribute->intValues.clear();
    attribute->boolValues.clear();
    const std::size_t pointCount = geometry.pointPositions.size() / 3;
    attribute->floatValues.assign(pointCount, value);
}

void setPrimitiveIntAttributeFromUInt32(
    GeometryData& geometry,
    const std::string& name,
    const std::vector<uint32_t>& values) {
    GeometryAttribute* attribute = findAttribute(geometry, name, GeometryAttributeDomain::Primitive);
    if (!attribute) {
        geometry.attributes.push_back({});
        attribute = &geometry.attributes.back();
    }

    attribute->name = name;
    attribute->domain = GeometryAttributeDomain::Primitive;
    attribute->dataType = GeometryAttributeDataType::Int;
    attribute->tupleSize = 1;
    attribute->floatValues.clear();
    attribute->boolValues.clear();
    attribute->intValues.resize(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        attribute->intValues[index] = static_cast<int64_t>(values[index]);
    }
}

void ensureGeometryGroups(GeometryData& geometry) {
    const std::size_t triangleCount = geometry.triangleIndices.size() / 3;
    if (geometry.triangleGroupIds.size() != triangleCount) {
        geometry.triangleGroupIds.assign(triangleCount, 0u);
    }

    std::unordered_map<uint32_t, GeometryGroup> groupsById;
    groupsById.reserve(geometry.groups.size() + 4);
    for (const GeometryGroup& group : geometry.groups) {
        auto [it, inserted] = groupsById.emplace(group.id, group);
        if (inserted) {
            continue;
        }

        if (it->second.name.empty() && !group.name.empty()) {
            it->second.name = group.name;
        }
        if (it->second.source.empty() && !group.source.empty()) {
            it->second.source = group.source;
        }
    }

    for (uint32_t groupId : geometry.triangleGroupIds) {
        if (groupsById.find(groupId) != groupsById.end()) {
            continue;
        }

        GeometryGroup group{};
        group.id = groupId;
        group.name = (groupId == 0u) ? "Default" : defaultGroupName(groupId);
        group.source = "generated";
        groupsById.emplace(groupId, std::move(group));
    }

    if (groupsById.empty()) {
        GeometryGroup defaultGroup{};
        defaultGroup.id = 0;
        defaultGroup.name = "Default";
        defaultGroup.source = "generated";
        groupsById.emplace(defaultGroup.id, std::move(defaultGroup));
    }

    geometry.groups.clear();
    geometry.groups.reserve(groupsById.size());
    for (auto& entry : groupsById) {
        GeometryGroup group = std::move(entry.second);
        if (group.name.empty()) {
            group.name = (group.id == 0u) ? "Default" : defaultGroupName(group.id);
        }
        if (group.source.empty()) {
            group.source = "generated";
        }
        geometry.groups.push_back(std::move(group));
    }
    std::sort(geometry.groups.begin(), geometry.groups.end(), [](const GeometryGroup& lhs, const GeometryGroup& rhs) {
        return lhs.id < rhs.id;
    });

    setPrimitiveIntAttributeFromUInt32(geometry, "group.id", geometry.triangleGroupIds);
}

void refreshNodeDataBlockMetadata(NodeDataBlock& dataBlock) {
    dataBlock.metadata["data.type"] = nodeDataTypeToString(dataBlock.dataType);

    if (dataBlock.dataType == NodeDataType::Geometry ||
        dataBlock.dataType == NodeDataType::HeatReceiver ||
        dataBlock.dataType == NodeDataType::HeatSource) {
        if (dataBlock.geometry.sourceModelPath.empty()) {
            dataBlock.metadata.erase("geometry.model_path");
        } else {
            dataBlock.metadata["geometry.model_path"] = dataBlock.geometry.sourceModelPath;
        }
        dataBlock.metadata["geometry.model_id"] = std::to_string(dataBlock.geometry.modelId);
        dataBlock.metadata["geometry.point_count"] = std::to_string(dataBlock.geometry.pointPositions.size() / 3);
        dataBlock.metadata["geometry.triangle_count"] = std::to_string(dataBlock.geometry.triangleIndices.size() / 3);
        dataBlock.metadata["geometry.group_count"] = std::to_string(dataBlock.geometry.groups.size());
        dataBlock.metadata["geometry.groups"] = geometryGroupSummary(dataBlock.geometry);
        dataBlock.metadata["geometry.group_names"] = geometryGroupNamesList(dataBlock.geometry);
        dataBlock.metadata["geometry.group_names.vertex"] =
            geometryGroupNamesListForType(dataBlock.geometry, "geometry.group_names.vertex");
        dataBlock.metadata["geometry.group_names.object"] =
            geometryGroupNamesListForType(dataBlock.geometry, "geometry.group_names.object");
        dataBlock.metadata["geometry.group_names.material"] =
            geometryGroupNamesListForType(dataBlock.geometry, "geometry.group_names.material");
        dataBlock.metadata["geometry.group_names.smooth"] =
            geometryGroupNamesListForType(dataBlock.geometry, "geometry.group_names.smooth");
        dataBlock.metadata["geometry.attribute_count"] = std::to_string(dataBlock.geometry.attributes.size());
        dataBlock.metadata["geometry.attributes"] = geometryAttributeSummary(dataBlock.geometry);
    } else {
        dataBlock.metadata.erase("geometry.model_path");
        dataBlock.metadata.erase("geometry.model_id");
        dataBlock.metadata.erase("geometry.point_count");
        dataBlock.metadata.erase("geometry.triangle_count");
        dataBlock.metadata.erase("geometry.group_count");
        dataBlock.metadata.erase("geometry.groups");
        dataBlock.metadata.erase("geometry.group_names");
        dataBlock.metadata.erase("geometry.group_names.vertex");
        dataBlock.metadata.erase("geometry.group_names.object");
        dataBlock.metadata.erase("geometry.group_names.material");
        dataBlock.metadata.erase("geometry.group_names.smooth");
        dataBlock.metadata.erase("geometry.attribute_count");
        dataBlock.metadata.erase("geometry.attributes");
    }
}

void seedOutputDataBlocksFromInputs(
    const NodeGraphNode& node,
    const std::vector<const NodeDataBlock*>& inputs,
    std::vector<NodeDataBlock>& outputs) {
    std::unordered_map<std::string, std::string> mergedMetadata;
    std::vector<NodeGraphNodeId> mergedLineage;
    std::unordered_set<uint32_t> seenNodeIds;

    for (const NodeDataBlock* input : inputs) {
        if (!input) {
            continue;
        }

        for (const auto& metadataEntry : input->metadata) {
            mergedMetadata[metadataEntry.first] = metadataEntry.second;
        }

        for (NodeGraphNodeId lineageNodeId : input->lineageNodeIds) {
            if (!lineageNodeId.isValid()) {
                continue;
            }

            if (seenNodeIds.insert(lineageNodeId.value).second) {
                mergedLineage.push_back(lineageNodeId);
            }
        }
    }

    if (node.id.isValid() && seenNodeIds.insert(node.id.value).second) {
        mergedLineage.push_back(node.id);
    }

    mergedMetadata["graph.producer_node_id"] = std::to_string(node.id.value);
    mergedMetadata["graph.producer_type_id"] = canonicalNodeTypeId(node.typeId);
    mergedMetadata["graph.lineage_depth"] = std::to_string(mergedLineage.size());

    for (NodeDataBlock& output : outputs) {
        output = {};
        output.metadata = mergedMetadata;
        output.lineageNodeIds = mergedLineage;
        refreshNodeDataBlockMetadata(output);
    }
}
