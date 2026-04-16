#pragma once

#include <cstdint>
#include <vector>

#include "domain/HeatData.hpp"
#include "heat/HeatSystemPresets.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"
#include "nodegraph/NodeGraphRuntime.hpp"
#include "runtime/RuntimePackageGraph.hpp"
#include "runtime/RuntimePackages.hpp"

class NodeGraphRuntimeBridge;
class NodeGraphTopology;
class NodePayloadRegistry;
class RuntimeProductRegistry;

class RuntimePackageCompiler {
public:
    void setRuntimeBridge(const NodeGraphRuntimeBridge* runtimeBridge);
    void setRuntimeProductRegistry(const RuntimeProductRegistry* runtimeProductRegistry);
    ModelPackage buildModelPackage(
        const GeometryData& geometry) const;
    RemeshPackage buildRemeshPackage(
        const NodeGraphNode& node,
        const RemeshData& remesh,
        const NodePayloadRegistry* payloadRegistry,
        const NodeDataHandle& remeshHandle = {}) const;
    VoronoiPackage buildVoronoiPackage(
        const NodeGraphNode& node,
        const NodePayloadRegistry* payloadRegistry,
        const VoronoiData& voronoi) const;
    HeatPackage buildHeatPackage(
        const NodeGraphNode& node,
        const NodeGraphTopology& topology,
        const NodePayloadRegistry* payloadRegistry,
        const HeatData& heat,
        const ProductHandle& voronoiProduct,
        const ProductHandle& contactProduct) const;
    ContactPackage buildContactPackage(
        const NodeGraphNode& node,
        const NodePayloadRegistry* payloadRegistry,
        const ContactData& contact) const;
    RuntimePackageGraph buildRuntimePackageGraph(
        const NodeGraphState& graphState,
        const NodeGraphEvaluationState& evaluationState,
        const NodePayloadRegistry* payloadRegistry) const;

private:
    std::vector<RuntimeThermalMaterial> buildRuntimeThermalMaterials(
        const NodeGraphTopology& topology,
        const std::vector<ProductHandle>& receiverRemeshProducts,
        const std::vector<HeatMaterialBinding>& materialBindings) const;

    const NodeGraphRuntimeBridge* runtimeBridge = nullptr;
    const RuntimeProductRegistry* runtimeProductRegistry = nullptr;
};
