#include "NodeGraphDataTypes.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphTypeRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodePayloadRegistry.hpp"
#include "domain/HeatModelData.hpp"
#include "domain/SerialTemperatureData.hpp"

#include <cstddef>

void populateMetadata(NodeDataBlock& dataBlock, const NodeGraphTypeRegistry* typeRegistry, const NodePayloadRegistry* registry) {
    const std::string* typeName = typeRegistry ? typeRegistry->getTypeName(dataBlock.dataType) : nullptr;
    dataBlock.metadata["data.type"] = typeName ? *typeName : "unknown";

    if (dataBlock.dataType == payloadtypes::Geometry ||
        dataBlock.dataType == payloadtypes::Remesh ||
        dataBlock.dataType == payloadtypes::HeatModel) {
        const GeometryData* geometry = registry ? registry->resolveGeometry(dataBlock.payloadHandle) : nullptr;
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

    if (dataBlock.dataType == payloadtypes::HeatModel) {
        const HeatModelData* heatModel = registry ? registry->get<HeatModelData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat_model.boundary_condition"] =
            heatModel ? std::to_string(static_cast<uint32_t>(heatModel->boundaryCondition.type)) : std::string();
        dataBlock.metadata["heat_model.dirichlet_temperature_c"] =
            heatModel ? std::to_string(heatModel->boundaryCondition.temperatureC) : std::string();
        dataBlock.metadata["heat_model.initial_temperature_c"] =
            heatModel ? std::to_string(heatModel->initialTemperatureC) : std::string();
    } else {
        dataBlock.metadata.erase("heat_model.boundary_condition");
        dataBlock.metadata.erase("heat_model.fixed_temperature");
        dataBlock.metadata.erase("heat_model.initial_temperature");
        dataBlock.metadata.erase("heat_model.dirichlet_temperature_c");
        dataBlock.metadata.erase("heat_model.initial_temperature_c");
    }

    if (dataBlock.dataType == payloadtypes::SerialTemperature) {
        const auto* serial = registry ? registry->get<SerialTemperatureData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["serial.enabled"] = (serial && serial->enabled) ? "true" : "false";
        dataBlock.metadata["serial.port"] = serial ? serial->portName : std::string();
        dataBlock.metadata["serial.baud_rate"] = std::to_string(serial ? serial->baudRate : 0u);
    } else {
        dataBlock.metadata.erase("serial.enabled");
        dataBlock.metadata.erase("serial.port");
        dataBlock.metadata.erase("serial.baud_rate");
    }

    if (dataBlock.dataType == payloadtypes::Heat) {
        const HeatData* heatData = registry ? registry->get<HeatData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["heat.active"] = (heatData && heatData->active) ? "true" : "false";
        dataBlock.metadata["heat.model_count"] =
            std::to_string(heatData ? heatData->heatModelHandles.size() : 0u);
    } else {
        dataBlock.metadata.erase("heat.active");
        dataBlock.metadata.erase("heat.model_count");
    }

    if (dataBlock.dataType == payloadtypes::Voronoi) {
        const VoronoiData* voronoiData = registry ? registry->get<VoronoiData>(dataBlock.payloadHandle) : nullptr;
        dataBlock.metadata["voronoi.active"] = (voronoiData && voronoiData->active) ? "true" : "false";
        dataBlock.metadata["voronoi.model_count"] =
            std::to_string(voronoiData ? voronoiData->modelMeshHandles.size() : 0u);
        dataBlock.metadata["voronoi.cell_size"] =
            voronoiData ? std::to_string(voronoiData->cellSize) : std::string();
        dataBlock.metadata["voronoi.voxel_resolution"] =
            voronoiData ? std::to_string(voronoiData->voxelResolution) : std::string();
    } else {
        dataBlock.metadata.erase("voronoi.active");
        dataBlock.metadata.erase("voronoi.receiver_count");
        dataBlock.metadata.erase("voronoi.cell_size");
        dataBlock.metadata.erase("voronoi.voxel_resolution");
    }
}
