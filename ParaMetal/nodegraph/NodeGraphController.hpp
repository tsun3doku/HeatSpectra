#pragma once

#include "NodeGraphDisplay.hpp"
#include "NodeGraphCompiler.hpp"
#include "NodeGraphRuntime.hpp"
#include "runtime/RuntimeConnections.hpp"
#include "runtime/package/RuntimePackageCompiler.hpp"
#include "runtime/package/RuntimePackageController.hpp"
#include "util/Units.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class VulkanDevice;
class MemoryAllocator;
class NodePayloadRegistry;

class NodeGraphController {
public:
    NodeGraphController(
        NodePayloadRegistry& payloadRegistry,
        const RuntimeConnections& connections,
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator);

    void tick();
    void resetGraph(const NodeGraphState& state);
    bool applyGraphDelta(const NodeGraphDelta& delta);
    const NodeGraphState& graphState() const;
    bool resolveGizmoTransformNode(uint64_t outputSocketKey, NodeGraphNodeId& outNodeId) const;
    void updateDisplayTransports();
    void setWorldUnit(units::LengthUnit unit);
    units::LengthUnit worldUnit() const { return activeWorldUnit; }
    const NodeGraphCompiled& compiledState() const;

    bool runtimeModelIdsForNode(NodeGraphNodeId nodeId,
                                 std::vector<uint32_t>& outIds) const;

    RuntimeProductManager* getProductManager() { return packageController.products(); }
    RuntimePackageManager* getPackageManager() { return &packageController.packages(); }

private:
    void rebuildForDelta(const NodeGraphDelta& delta);
    bool compileRuntimePackages();
    bool runtimeModelIdsForSocket(uint64_t socketKey,
                                   std::vector<uint32_t>& outIds) const;
    void addRuntimeModelId(std::vector<uint32_t>& outIds, uint32_t id) const;

    NodePayloadRegistry& payloadRegistry;
    RuntimeConnections runtimeConnections{};
    NodeGraphRuntime runtime;
    bool packageCompilationPending = false;
    bool frozenPackageRebuildPending = false;
    NodeGraphCompiled plan{};
    NodeGraphDisplay nodeGraphDisplay{};
    RuntimePackageCompiler packageCompiler{};
    RuntimePackageController packageController;
    units::LengthUnit activeWorldUnit = units::defaultLengthUnit();
};
