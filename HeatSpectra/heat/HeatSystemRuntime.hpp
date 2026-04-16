#pragma once

#include "mesh/remesher/SupportingHalfedge.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class CommandPool;
class HeatSourceRuntime;
class MemoryAllocator;
class VulkanDevice;

class HeatSystemRuntime {
public:
    HeatSystemRuntime() = default;
    ~HeatSystemRuntime() = default;

    struct SourceBinding {
        uint32_t runtimeModelId = 0;
        std::unique_ptr<HeatSourceRuntime> heatSource;
    };

    const SourceBinding* findBaseSourceBinding() const;

    void setSourcePayloads(
        const std::vector<SupportingHalfedge::IntrinsicMesh>& sourceIntrinsicMeshes,
        const std::vector<uint32_t>& sourceRuntimeModelIds,
        const std::unordered_map<uint32_t, float>& sourceTemperatureByRuntimeId);
    bool ensureModelBindings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool);
    bool needsRebuild() const { return modelBindingsDirty; }

    std::vector<SourceBinding>& getSourceBindingsMutable() { return sourceBindings; }

    void cleanupModelBindings();

private:
    std::vector<SupportingHalfedge::IntrinsicMesh> activeSourceIntrinsicMeshes;
    std::vector<uint32_t> activeSourceRuntimeModelIds;
    std::unordered_map<uint32_t, float> activeSourceTemperatureByRuntimeId;
    std::vector<SourceBinding> sourceBindings;
    bool modelBindingsDirty = true;
};
