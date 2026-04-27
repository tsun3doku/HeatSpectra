#include "NodeGraphDataTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphPayloadTypes.hpp"
#include "NodePayloadRegistry.hpp"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>

void populateMetadata(NodeDataBlock& dataBlock, const NodePayloadRegistry* registry) {
    dataBlock.metadata["data.type"] = nodePayloadTypeName(dataBlock.dataType);

    if (dataBlock.dataType == NodePayloadType::Geometry ||
        dataBlock.dataType == NodePayloadType::Remesh ||
        dataBlock.dataType == NodePayloadType::HeatReceiver ||
        dataBlock.dataType == NodePayloadType::HeatSource) {
        const GeometryData* geometry = registry ? registry->resolveGeometry(dataBlock.dataType, dataBlock.payloadHandle) : nullptr;
        if (!geometry || geometry->baseModelPath.empty()) {
            dataBlock.metadata.erase("geometry.model_path");
        } else {
            dataBlock.metadata["geometry.model_path"] = geometry->baseModelPath;
        }
        const size_t pointCount = geometry ? geometry->pointPositions.size() / 3 : 0u;
        const size_t triangleCount = geometry ? geometry->triangleIndices.size() / 3 : 0u;
        dataBlock.metadata.erase("geometry.model_id");
        dataBlock.metadata["geometry.point_count"] = std::to_string(pointCount);
        dataBlock.metadata["geometry.triangle_count"] = std::to_string(triangleCount);
    } else {
        dataBlock.metadata.erase("geometry.model_path");
        dataBlock.metadata.erase("geometry.model_id");
        dataBlock.metadata.erase("geometry.point_count");
        dataBlock.metadata.erase("geometry.triangle_count");
    }

    dataBlock.metadata.erase("geometry.group_count");
    dataBlock.metadata.erase("geometry.groups");
    dataBlock.metadata.erase("geometry.group_names");
    dataBlock.metadata.erase("geometry.group_names.vertex");
    dataBlock.metadata.erase("geometry.group_names.object");
    dataBlock.metadata.erase("geometry.group_names.material");
    dataBlock.metadata.erase("geometry.group_names.smooth");

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

void buildOutputs(const NodeGraphNode& node, const std::vector<const NodeDataBlock*>& inputs, std::vector<NodeDataBlock>& outputs) {
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
        populateMetadata(output);
    }
}
