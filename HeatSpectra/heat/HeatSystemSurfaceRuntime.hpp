#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "HeatReceiverRuntime.hpp"
#include "runtime/RuntimePackages.hpp"

class CommandPool;
class HeatSystemSimRuntime;
class MemoryAllocator;
class NodePayloadRegistry;
class RuntimeIntrinsicCache;
class VulkanDevice;
class VoronoiModelRuntime;

class HeatSystemSurfaceRuntime {
public:
    HeatSystemSurfaceRuntime() = default;
    ~HeatSystemSurfaceRuntime();

    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& getReceivers() const { return receiverRuntimes; }

    bool initializeReceiverBindings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const RuntimeIntrinsicCache& intrinsicCache,
        const HeatPackage& heatPackage,
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>* voronoiModelRuntimes = nullptr);
    void refreshDescriptors(
        const HeatSystemSimRuntime& simRuntime,
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorPool surfacePool,
        bool forceReallocate);
    void executeBufferTransfers(CommandPool& renderCommandPool);
    bool resetSurfaceTemperatures(CommandPool& renderCommandPool);
    void cleanup();

private:
    std::vector<std::unique_ptr<HeatReceiverRuntime>> receiverRuntimes;
};
