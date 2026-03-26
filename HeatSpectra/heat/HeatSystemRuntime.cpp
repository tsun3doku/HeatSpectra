#include "HeatSystemRuntime.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "HeatSourceRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>
#include <unordered_set>

const HeatSystemRuntime::SourceBinding* HeatSystemRuntime::findBaseSourceBinding() const {
    for (const SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.geometryPackage.runtimeModelId != 0 && sourceBinding.heatSource) {
            return &sourceBinding;
        }
    }

    return nullptr;
}

void HeatSystemRuntime::initializeModelBindings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& renderCommandPool,
    const HeatPackage& heatPackage) {
    cleanupModelBindings();

    const std::size_t pairCount = heatPackage.sourceRuntimeModelIds.size();
    std::unordered_set<uint32_t> seenSourceIds;
    for (std::size_t index = 0; index < pairCount; ++index) {
        const uint32_t sourceId = heatPackage.sourceRuntimeModelIds[index];
        if (sourceId == 0 || !seenSourceIds.insert(sourceId).second) {
            continue;
        }

        const GeometryData& geometry = heatPackage.sourceGeometries[index];
        if (geometry.intrinsicHandle.key == 0) {
            continue;
        }

        SourceBinding sourceBinding{};
        sourceBinding.geometryPackage.geometry = geometry;
        sourceBinding.geometryPackage.runtimeModelId = sourceId;
        const auto tempIt = heatPackage.sourceTemperatureByRuntimeId.find(sourceId);
        const float initialTemperature =
            (tempIt != heatPackage.sourceTemperatureByRuntimeId.end()) ? tempIt->second : 100.0f;
        sourceBinding.heatSource = std::make_unique<HeatSourceRuntime>(
            vulkanDevice,
            memoryAllocator,
            geometry,
            heatPackage.sourceIntrinsics[index],
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
}

void HeatSystemRuntime::cleanupModelBindings() {
    for (SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.heatSource) {
            sourceBinding.heatSource->cleanup();
        }
    }
    sourceBindings.clear();
}
