#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "heat/HeatSystemPresets.hpp"
#include "runtime/RuntimePackages.hpp"

class NodePayloadRegistry;
class SceneController;

class RuntimePackageCompiler {
public:
    void setSceneController(SceneController* sceneController);
    bool resolveRuntimeModel(const GeometryData& geometry, uint32_t& outRuntimeModelId) const;
    GeometryPackage buildGeometryPackage(const GeometryData& geometry) const;
    VoronoiPackage buildVoronoiPackage(
        const NodePayloadRegistry* payloadRegistry,
        const VoronoiData& voronoi) const;
    HeatPackage buildHeatPackage(
        const NodePayloadRegistry* payloadRegistry,
        const HeatData& heat,
        const VoronoiData* voronoi) const;
    ContactPackage buildContactPackage(
        const NodePayloadRegistry* payloadRegistry,
        const ContactData& contact) const;

private:
    bool tryParseHeatMaterialModelId(const std::string& value, uint32_t& outNodeModelId) const;
    std::vector<RuntimeThermalMaterial> buildRuntimeThermalMaterials(
        const std::vector<GeometryData>& receiverGeometries,
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        const std::vector<HeatMaterialBindingEntry>& materialBindings) const;

    SceneController* sceneController = nullptr;
};
