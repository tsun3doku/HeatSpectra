#include "NodeGraphDataTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphPayloadTypes.hpp"
#include "NodePayloadRegistry.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

static std::atomic<uint64_t> geometryRevisionCounter{1};

const char* nodeDataTypeName(NodeDataType dataType) {
    switch (dataType) {
    case NodeDataType::Geometry:
        return "geometry";
    case NodeDataType::Intrinsic:
        return "intrinsic";
    case NodeDataType::HeatReceiver:
        return "heat_receiver";
    case NodeDataType::HeatSource:
        return "heat_source";
    case NodeDataType::Contact:
        return "contact";
    case NodeDataType::Heat:
        return "heat";
    case NodeDataType::Voronoi:
        return "voronoi";
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

const GeometryData* resolveGeometryForDataBlock(const NodeDataBlock& dataBlock, const NodePayloadRegistry* registry) {
    if (!registry || dataBlock.payloadHandle.key == 0) {
        return nullptr;
    }

    switch (dataBlock.dataType) {
    case NodeDataType::Geometry:
        return registry->get<GeometryData>(dataBlock.payloadHandle);
    case NodeDataType::HeatReceiver: {
        const HeatReceiverData* heatReceiver = registry->get<HeatReceiverData>(dataBlock.payloadHandle);
        return heatReceiver ? registry->get<GeometryData>(heatReceiver->geometryHandle) : nullptr;
    }
    case NodeDataType::HeatSource: {
        const HeatSourceData* heatSource = registry->get<HeatSourceData>(dataBlock.payloadHandle);
        return heatSource ? registry->get<GeometryData>(heatSource->geometryHandle) : nullptr;
    }
    default:
        return nullptr;
    }
}

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

void setGeometryDetailBool(GeometryData& geometry, const std::string& name, bool value) {
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

void setGeometryPrimitiveIntAttribute(
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

void normalizeGeometryGroups(GeometryData& geometry) {
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

    setGeometryPrimitiveIntAttribute(geometry, "group.id", geometry.triangleGroupIds);
}

void bumpGeometryRevision(GeometryData& geometry) {
    geometry.geometryRevision = geometryRevisionCounter.fetch_add(1, std::memory_order_relaxed);
}

void updateDataBlockMetadata(NodeDataBlock& dataBlock, const NodePayloadRegistry* registry) {
    dataBlock.metadata["data.type"] = nodeDataTypeName(dataBlock.dataType);

    if (dataBlock.dataType == NodeDataType::Geometry ||
        dataBlock.dataType == NodeDataType::HeatReceiver ||
        dataBlock.dataType == NodeDataType::HeatSource) {
        const GeometryData* geometry = resolveGeometryForDataBlock(dataBlock, registry);
        if (!geometry || geometry->baseModelPath.empty()) {
            dataBlock.metadata.erase("geometry.model_path");
        } else {
            dataBlock.metadata["geometry.model_path"] = geometry->baseModelPath;
        }
        const uint32_t modelId = geometry ? geometry->modelId : 0u;
        const uint64_t revision = geometry ? geometry->geometryRevision : 0u;
        const size_t pointCount = geometry ? geometry->pointPositions.size() / 3 : 0u;
        const size_t triangleCount = geometry ? geometry->triangleIndices.size() / 3 : 0u;
        const size_t groupCount = geometry ? geometry->groups.size() : 0u;
        dataBlock.metadata["geometry.model_id"] = std::to_string(modelId);
        dataBlock.metadata["geometry.revision"] = std::to_string(revision);
        dataBlock.metadata["geometry.point_count"] = std::to_string(pointCount);
        dataBlock.metadata["geometry.triangle_count"] = std::to_string(triangleCount);
        dataBlock.metadata["geometry.group_count"] = std::to_string(groupCount);
        dataBlock.metadata["geometry.groups"] = geometry ? geometryGroupSummary(*geometry) : "none";
        dataBlock.metadata["geometry.group_names"] = geometry ? geometryGroupNamesList(*geometry) : "none";
        dataBlock.metadata["geometry.group_names.vertex"] =
            geometry ? geometryGroupNamesListForType(*geometry, "geometry.group_names.vertex") : std::string();
        dataBlock.metadata["geometry.group_names.object"] =
            geometry ? geometryGroupNamesListForType(*geometry, "geometry.group_names.object") : std::string();
        dataBlock.metadata["geometry.group_names.material"] =
            geometry ? geometryGroupNamesListForType(*geometry, "geometry.group_names.material") : std::string();
        dataBlock.metadata["geometry.group_names.smooth"] =
            geometry ? geometryGroupNamesListForType(*geometry, "geometry.group_names.smooth") : std::string();
        dataBlock.metadata["geometry.attribute_count"] = std::to_string(geometry ? geometry->attributes.size() : 0u);
        dataBlock.metadata["geometry.attributes"] = geometry ? geometryAttributeSummary(*geometry) : "none";
    } else {
        dataBlock.metadata.erase("geometry.model_path");
        dataBlock.metadata.erase("geometry.model_id");
        dataBlock.metadata.erase("geometry.revision");
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

    if (dataBlock.dataType == NodeDataType::Intrinsic) {
        const IntrinsicMeshData* intrinsic = registry ? registry->get<IntrinsicMeshData>(dataBlock.payloadHandle) : nullptr;
        const size_t vertexCount = intrinsic ? intrinsic->vertices.size() : 0u;
        const size_t triangleCount = intrinsic ? intrinsic->triangleIndices.size() / 3 : 0u;
        dataBlock.metadata["intrinsic.vertex_count"] = std::to_string(vertexCount);
        dataBlock.metadata["intrinsic.triangle_count"] = std::to_string(triangleCount);
        dataBlock.metadata["intrinsic.face_count"] = std::to_string(intrinsic ? intrinsic->faceIds.size() : 0u);
    } else {
        dataBlock.metadata.erase("intrinsic.vertex_count");
        dataBlock.metadata.erase("intrinsic.triangle_count");
        dataBlock.metadata.erase("intrinsic.face_count");
    }

    if (dataBlock.dataType == NodeDataType::HeatSource) {
        const HeatSourceData* heatSource = registry ? registry->get<HeatSourceData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat_source.temperature"] =
            heatSource ? std::to_string(heatSource->temperature) : std::string();
    } else {
        dataBlock.metadata.erase("heat_source.temperature");
    }

    if (dataBlock.dataType == NodeDataType::HeatReceiver) {
        const HeatReceiverData* heatReceiver = registry ? registry->get<HeatReceiverData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat_receiver.active"] = heatReceiver ? "true" : "false";
    } else {
        dataBlock.metadata.erase("heat_receiver.active");
    }

    if (dataBlock.dataType == NodeDataType::Heat) {
        const HeatData* heatData = registry ? registry->get<HeatData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat.active"] = (heatData && heatData->active) ? "true" : "false";
        dataBlock.metadata["heat.paused"] = (heatData && heatData->paused) ? "true" : "false";
        dataBlock.metadata["heat.reset_requested"] = (heatData && heatData->resetRequested) ? "true" : "false";
        dataBlock.metadata["heat.source_count"] =
            std::to_string(heatData ? heatData->sourceHandles.size() : 0u);
        dataBlock.metadata["heat.receiver_count"] =
            std::to_string(heatData ? heatData->receiverGeometryHandles.size() : 0u);
        dataBlock.metadata["heat.material_binding_count"] =
            std::to_string(heatData ? heatData->materialBindings.size() : 0u);
    } else {
        dataBlock.metadata.erase("heat.active");
        dataBlock.metadata.erase("heat.paused");
        dataBlock.metadata.erase("heat.reset_requested");
        dataBlock.metadata.erase("heat.source_count");
        dataBlock.metadata.erase("heat.receiver_count");
        dataBlock.metadata.erase("heat.material_binding_count");
    }

    if (dataBlock.dataType == NodeDataType::Contact) {
        const ContactData* contactData = registry ? registry->get<ContactData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["contact.active"] = (contactData && contactData->active) ? "true" : "false";
        dataBlock.metadata["contact.binding_count"] =
            std::to_string(contactData ? contactData->bindings.size() : 0u);
    } else {
        dataBlock.metadata.erase("contact.active");
        dataBlock.metadata.erase("contact.binding_count");
    }

    if (dataBlock.dataType == NodeDataType::Voronoi) {
        const VoronoiData* voronoiData = registry ? registry->get<VoronoiData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["voronoi.active"] = (voronoiData && voronoiData->active) ? "true" : "false";
        dataBlock.metadata["voronoi.receiver_count"] =
            std::to_string(voronoiData ? voronoiData->receiverGeometryHandles.size() : 0u);
        dataBlock.metadata["voronoi.cell_size"] =
            voronoiData ? std::to_string(voronoiData->params.cellSize) : std::string();
        dataBlock.metadata["voronoi.voxel_resolution"] =
            voronoiData ? std::to_string(voronoiData->params.voxelResolution) : std::string();
    } else {
        dataBlock.metadata.erase("voronoi.active");
        dataBlock.metadata.erase("voronoi.receiver_count");
        dataBlock.metadata.erase("voronoi.cell_size");
        dataBlock.metadata.erase("voronoi.voxel_resolution");
    }
}

void initializeOutputsFromInputs(
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
    mergedMetadata["graph.producer_type_id"] = getNodeTypeId(node.typeId);
    mergedMetadata["graph.lineage_depth"] = std::to_string(mergedLineage.size());

    for (NodeDataBlock& output : outputs) {
        output = {};
        output.metadata = mergedMetadata;
        output.lineageNodeIds = mergedLineage;
        updateDataBlockMetadata(output);
    }
}
