#pragma once

#include <cstdint>

#include "domain/HeatData.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"
#include "nodegraph/NodeGraphRuntime.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

class NodeGraphRuntimeBridge;
class NodePayloadRegistry;

class RuntimePackageCompiler {
public:
    void setRuntimeBridge(const NodeGraphRuntimeBridge* runtimeBridge);

    ModelPackage buildModelPackage(
        const GeometryData& geometry) const;
    RemeshPackage buildRemeshPackage(
        const NodeGraphNode& node,
        const RemeshData& remesh,
        const NodePayloadRegistry* payloadRegistry,
        const ECSRegistry& registry,
        const NodeDataHandle& remeshHandle = {}) const;
    VoronoiPackage buildVoronoiPackage(
        const NodeGraphNode& node,
        const NodePayloadRegistry* payloadRegistry,
        const ECSRegistry& registry,
        const VoronoiData& voronoi) const;
    HeatPackage buildHeatPackage(
        const NodeGraphNode& node,
        const NodePayloadRegistry* payloadRegistry,
        const ECSRegistry& registry,
        const HeatData& heat,
        const ProductHandle& voronoiProduct,
        const ProductHandle& contactProduct) const;
    ContactPackage buildContactPackage(
        const NodeGraphNode& node,
        const NodePayloadRegistry* payloadRegistry,
        const ECSRegistry& registry,
        const ContactData& contact) const;

    void compileAndApply(
        const NodeGraphState& graphState,
        const NodeGraphEvaluationState& evaluationState,
        const NodePayloadRegistry* payloadRegistry,
        ECSRegistry& registry,
        std::unordered_set<ECSEntity>& staleEntities) const;

private:
    template <typename PackageT>
    static void applyPackage(ECSRegistry& registry, uint64_t socketKey, const PackageT& pkg, std::unordered_set<ECSEntity>& staleEntities);

    const NodeGraphRuntimeBridge* runtimeBridge = nullptr;
};
