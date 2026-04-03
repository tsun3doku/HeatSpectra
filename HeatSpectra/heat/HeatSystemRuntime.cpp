#include "HeatSystemRuntime.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "HeatSourceRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>
#include <unordered_set>

const HeatSystemRuntime::SourceBinding* HeatSystemRuntime::findBaseSourceBinding() const {
    for (const SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.runtimeModelId != 0 && sourceBinding.heatSource) {
            return &sourceBinding;
        }
    }

    return nullptr;
}

void HeatSystemRuntime::setSourcePayloads(
    const std::vector<GeometryData>& sourceGeometries,
    const std::vector<SupportingHalfedge::IntrinsicMesh>& sourceIntrinsicMeshes,
    const std::vector<uint32_t>& sourceRuntimeModelIds,
    const std::unordered_map<uint32_t, float>& sourceTemperatureByRuntimeId) {
    activeSourceGeometries = sourceGeometries;
    activeSourceIntrinsicMeshes = sourceIntrinsicMeshes;
    activeSourceRuntimeModelIds = sourceRuntimeModelIds;
    activeSourceTemperatureByRuntimeId = sourceTemperatureByRuntimeId;
    modelBindingsDirty = true;
}

bool HeatSystemRuntime::ensureModelBindings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& renderCommandPool) {
    if (!modelBindingsDirty) {
        return true;
    }

    cleanupModelBindings();

    const std::size_t pairCount = std::min(
        activeSourceRuntimeModelIds.size(),
        std::min(activeSourceGeometries.size(), activeSourceIntrinsicMeshes.size()));
    std::unordered_set<uint32_t> seenSourceIds;
    for (std::size_t index = 0; index < pairCount; ++index) {
        const uint32_t sourceId = activeSourceRuntimeModelIds[index];
        if (sourceId == 0 || !seenSourceIds.insert(sourceId).second) {
            continue;
        }

        const GeometryData& geometry = activeSourceGeometries[index];
        SourceBinding sourceBinding{};
        sourceBinding.geometry = geometry;
        sourceBinding.runtimeModelId = sourceId;
        const auto tempIt = activeSourceTemperatureByRuntimeId.find(sourceId);
        const float initialTemperature =
            (tempIt != activeSourceTemperatureByRuntimeId.end()) ? tempIt->second : 100.0f;
        sourceBinding.heatSource = std::make_unique<HeatSourceRuntime>(
            vulkanDevice,
            memoryAllocator,
            geometry,
            activeSourceIntrinsicMeshes[index],
            renderCommandPool,
            initialTemperature);
        if (!sourceBinding.heatSource || !sourceBinding.heatSource->isInitialized()) {
            std::cerr << "[HeatSystemRuntime] Failed to initialize heat source for model " << sourceId << std::endl;
            if (sourceBinding.heatSource) {
                sourceBinding.heatSource->cleanup();
            }
            continue;
        }
        sourceBindings.push_back(std::move(sourceBinding));
    }

    modelBindingsDirty = false;
    return true;
}

void HeatSystemRuntime::cleanupModelBindings() {
    for (SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.heatSource) {
            sourceBinding.heatSource->cleanup();
        }
    }
    sourceBindings.clear();
    modelBindingsDirty = true;
}
