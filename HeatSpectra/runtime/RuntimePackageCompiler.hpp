#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "heat/HeatSystemPresets.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"
#include "nodegraph/NodeGraphRuntime.hpp"
#include "runtime/RuntimePackages.hpp"

class NodeGraphRuntimeBridge;
class NodePayloadRegistry;
class RuntimeProductRegistry;
class SceneController;

class RuntimePackageCompiler {
public:
    void setSceneController(SceneController* sceneController);
    void setRuntimeBridge(const NodeGraphRuntimeBridge* runtimeBridge);
    void setRuntimeProductRegistry(const RuntimeProductRegistry* runtimeProductRegistry);
    GeometryPackage buildGeometryPackage(uint64_t socketKey, const GeometryData& geometry) const;
    RemeshPackage buildRemeshPackage(
        const RemeshData& remesh,
        const NodePayloadRegistry* payloadRegistry,
        const NodeDataHandle& remeshHandle = {}) const;
    VoronoiPackage buildVoronoiPackage(
        const NodePayloadRegistry* payloadRegistry,
        const VoronoiData& voronoi) const;
    HeatPackage buildHeatPackage(
        const NodePayloadRegistry* payloadRegistry,
        const HeatData& heat,
        const ProductHandle& voronoiProduct,
        const ProductHandle& contactProduct) const;
    ContactPackage buildContactPackage(
        const NodePayloadRegistry* payloadRegistry,
        const ContactData& contact) const;
    RuntimePackageSet buildRuntimePackageSet(
        const NodeGraphState& graphState,
        const NodeGraphEvaluationState& evaluationState,
        const NodePayloadRegistry* payloadRegistry) const;

private:
    bool tryParseHeatMaterialModelId(const std::string& value, uint32_t& outNodeModelId) const;
    std::vector<RuntimeThermalMaterial> buildRuntimeThermalMaterials(
        const std::vector<ProductHandle>& receiverRemeshProducts,
        const std::vector<HeatMaterialBindingEntry>& materialBindings) const;

    SceneController* sceneController = nullptr;
    const NodeGraphRuntimeBridge* runtimeBridge = nullptr;
    const RuntimeProductRegistry* runtimeProductRegistry = nullptr;
};
