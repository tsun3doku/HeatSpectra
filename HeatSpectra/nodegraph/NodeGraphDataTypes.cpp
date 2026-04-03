#include "NodeGraphDataTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodePayloadRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

const char* nodePayloadTypeName(NodePayloadType payloadType) {
    switch (payloadType) {
    case NodePayloadType::Geometry:
        return "geometry";
    case NodePayloadType::Remesh:
        return "remesh";
    case NodePayloadType::HeatReceiver:
        return "heat_receiver";
    case NodePayloadType::HeatSource:
        return "heat_source";
    case NodePayloadType::Contact:
        return "contact";
    case NodePayloadType::Heat:
        return "heat";
    case NodePayloadType::Voronoi:
        return "voronoi";
    case NodePayloadType::None:
    default:
        return "none";
    }
}

const GeometryData* resolveGeometryForDataBlock(const NodeDataBlock& dataBlock, const NodePayloadRegistry* registry) {
    if (!registry || dataBlock.payloadHandle.key == 0) {
        return nullptr;
    }

    switch (dataBlock.dataType) {
    case NodePayloadType::Geometry:
        return registry->resolveGeometryHandle(dataBlock.payloadHandle);
    case NodePayloadType::Remesh: {
        const RemeshData* remesh = registry->get<RemeshData>(dataBlock.payloadHandle);
        if (!remesh) {
            return nullptr;
        }
        return registry->resolveGeometryHandle(remesh->sourceMeshHandle);
    }
    case NodePayloadType::HeatReceiver: {
        const HeatReceiverData* heatReceiver = registry->get<HeatReceiverData>(dataBlock.payloadHandle);
        if (!heatReceiver) {
            return nullptr;
        }
        NodeDataBlock meshBlock{};
        meshBlock.payloadHandle = heatReceiver->meshHandle;
        if (registry->get<RemeshData>(heatReceiver->meshHandle) != nullptr) {
            meshBlock.dataType = NodePayloadType::Remesh;
        } else {
            meshBlock.dataType = NodePayloadType::Geometry;
        }
        return resolveGeometryForDataBlock(meshBlock, registry);
    }
    case NodePayloadType::HeatSource: {
        const HeatSourceData* heatSource = registry->get<HeatSourceData>(dataBlock.payloadHandle);
        if (!heatSource) {
            return nullptr;
        }
        NodeDataBlock meshBlock{};
        meshBlock.payloadHandle = heatSource->meshHandle;
        if (registry->get<RemeshData>(heatSource->meshHandle) != nullptr) {
            meshBlock.dataType = NodePayloadType::Remesh;
        } else {
            meshBlock.dataType = NodePayloadType::Geometry;
        }
        return resolveGeometryForDataBlock(meshBlock, registry);
    }
    default:
        return nullptr;
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

void updatePayloadHash(GeometryData& geometry) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combineString(hash, geometry.baseModelPath);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(geometry.modelId));
    for (float value : geometry.localToWorld) {
        NodeGraphHash::combineFloat(hash, value);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(geometry.pointPositions.size()));
    for (float value : geometry.pointPositions) {
        NodeGraphHash::combineFloat(hash, value);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(geometry.triangleIndices.size()));
    for (uint32_t value : geometry.triangleIndices) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(value));
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(geometry.triangleGroupIds.size()));
    for (uint32_t value : geometry.triangleGroupIds) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(value));
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(geometry.groups.size()));
    for (const GeometryGroup& group : geometry.groups) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(group.id));
        NodeGraphHash::combineString(hash, group.name);
        NodeGraphHash::combineString(hash, group.source);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(geometry.attributes.size()));
    for (const GeometryAttribute& attribute : geometry.attributes) {
        NodeGraphHash::combineString(hash, attribute.name);
        NodeGraphHash::combine(hash, static_cast<uint64_t>(attribute.domain));
        NodeGraphHash::combine(hash, static_cast<uint64_t>(attribute.dataType));
        NodeGraphHash::combine(hash, static_cast<uint64_t>(attribute.tupleSize));
        NodeGraphHash::combine(hash, static_cast<uint64_t>(attribute.floatValues.size()));
        for (float value : attribute.floatValues) {
            NodeGraphHash::combineFloat(hash, value);
        }
        NodeGraphHash::combine(hash, static_cast<uint64_t>(attribute.intValues.size()));
        for (int64_t value : attribute.intValues) {
            NodeGraphHash::combine(hash, static_cast<uint64_t>(value));
        }
        NodeGraphHash::combine(hash, static_cast<uint64_t>(attribute.boolValues.size()));
        for (uint8_t value : attribute.boolValues) {
            NodeGraphHash::combine(hash, static_cast<uint64_t>(value));
        }
    }
    geometry.payloadHash = hash;
}

uint64_t payloadHashForDataBlock(const NodeDataBlock& dataBlock, const NodePayloadRegistry* registry) {
    if (!registry || dataBlock.payloadHandle.key == 0) {
        return 0;
    }

    switch (dataBlock.dataType) {
    case NodePayloadType::Geometry: {
        const GeometryData* payload = registry->get<GeometryData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Remesh: {
        const RemeshData* payload = registry->get<RemeshData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::HeatReceiver: {
        const HeatReceiverData* payload = registry->get<HeatReceiverData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::HeatSource: {
        const HeatSourceData* payload = registry->get<HeatSourceData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Contact: {
        const ContactData* payload = registry->get<ContactData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Heat: {
        const HeatData* payload = registry->get<HeatData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Voronoi: {
        const VoronoiData* payload = registry->get<VoronoiData>(dataBlock.payloadHandle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::None:
    default:
        return 0;
    }
}

void updateDataBlockMetadata(NodeDataBlock& dataBlock, const NodePayloadRegistry* registry) {
    dataBlock.metadata["data.type"] = nodePayloadTypeName(dataBlock.dataType);

    if (dataBlock.dataType == NodePayloadType::Geometry ||
        dataBlock.dataType == NodePayloadType::Remesh ||
        dataBlock.dataType == NodePayloadType::HeatReceiver ||
        dataBlock.dataType == NodePayloadType::HeatSource) {
        const GeometryData* geometry = resolveGeometryForDataBlock(dataBlock, registry);
        if (!geometry || geometry->baseModelPath.empty()) {
            dataBlock.metadata.erase("geometry.model_path");
        } else {
            dataBlock.metadata["geometry.model_path"] = geometry->baseModelPath;
        }
        const uint32_t modelId = geometry ? geometry->modelId : 0u;
        const size_t pointCount = geometry ? geometry->pointPositions.size() / 3 : 0u;
        const size_t triangleCount = geometry ? geometry->triangleIndices.size() / 3 : 0u;
        const size_t groupCount = geometry ? geometry->groups.size() : 0u;
        dataBlock.metadata["geometry.model_id"] = std::to_string(modelId);
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

    dataBlock.metadata.erase("intrinsic.vertex_count");
    dataBlock.metadata.erase("intrinsic.triangle_count");
    dataBlock.metadata.erase("intrinsic.face_count");

    dataBlock.metadata.erase("remesh.vertex_count");
    dataBlock.metadata.erase("remesh.triangle_count");
    dataBlock.metadata.erase("remesh.face_count");

    if (dataBlock.dataType == NodePayloadType::HeatSource) {
        const HeatSourceData* heatSource = registry ? registry->get<HeatSourceData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat_source.temperature"] =
            heatSource ? std::to_string(heatSource->temperature) : std::string();
    } else {
        dataBlock.metadata.erase("heat_source.temperature");
    }

    if (dataBlock.dataType == NodePayloadType::HeatReceiver) {
        const HeatReceiverData* heatReceiver = registry ? registry->get<HeatReceiverData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat_receiver.active"] = heatReceiver ? "true" : "false";
    } else {
        dataBlock.metadata.erase("heat_receiver.active");
    }

    if (dataBlock.dataType == NodePayloadType::Heat) {
        const HeatData* heatData = registry ? registry->get<HeatData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat.active"] = (heatData && heatData->active) ? "true" : "false";
        dataBlock.metadata["heat.paused"] = (heatData && heatData->paused) ? "true" : "false";
        dataBlock.metadata["heat.reset_requested"] = (heatData && heatData->resetRequested) ? "true" : "false";
        dataBlock.metadata["heat.source_count"] =
            std::to_string(heatData ? heatData->sourceHandles.size() : 0u);
        dataBlock.metadata["heat.receiver_count"] =
            std::to_string(heatData ? heatData->receiverMeshHandles.size() : 0u);
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

    if (dataBlock.dataType == NodePayloadType::Contact) {
        const ContactData* contactData = registry ? registry->get<ContactData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["contact.active"] = (contactData && contactData->active) ? "true" : "false";
        dataBlock.metadata["contact.binding_count"] =
            std::to_string((contactData && contactData->pair.hasValidContact) ? 1u : 0u);
    } else {
        dataBlock.metadata.erase("contact.active");
        dataBlock.metadata.erase("contact.binding_count");
    }

    if (dataBlock.dataType == NodePayloadType::Voronoi) {
        const VoronoiData* voronoiData = registry ? registry->get<VoronoiData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["voronoi.active"] = (voronoiData && voronoiData->active) ? "true" : "false";
        dataBlock.metadata["voronoi.receiver_count"] =
            std::to_string(voronoiData ? voronoiData->receiverMeshHandles.size() : 0u);
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
