#pragma once

#include "HeatSystemPresets.hpp"
#include "runtime/RuntimePackages.hpp"
#include "voronoi/VoronoiSurfaceRuntime.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

class MemoryAllocator;
class ResourceManager;
class RuntimeIntrinsicCache;
class UniformBufferManager;
class VulkanDevice;
class CommandPool;
class VoronoiSystem;

class VoronoiSystemController {
public:
    VoronoiSystemController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ResourceManager& resourceManager,
        UniformBufferManager& uniformBufferManager,
        RuntimeIntrinsicCache& intrinsicCache,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    void createVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);
    void recreateVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);
    void applyVoronoiPackage(const VoronoiPackage& voronoiPackage);
    VoronoiSystem* getVoronoiSystem() const;

private:
    std::unique_ptr<VoronoiSystem> buildVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    RuntimeIntrinsicCache& intrinsicCache;
    CommandPool& renderCommandPool;
    std::unique_ptr<VoronoiSystem> voronoiSystem;
    VoronoiSurfaceRuntime previewSurfaceRuntime;
    VoronoiPackage configuredVoronoiPackage{};
    uint32_t maxFramesInFlight = 0;
};
