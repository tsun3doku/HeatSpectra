#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "runtime/RuntimePackages.hpp"
#include "voronoi/VoronoiGeometryRuntime.hpp"

class CommandPool;
class MemoryAllocator;
class RuntimeIntrinsicCache;
class VulkanDevice;
class VoronoiModelRuntime;

class VoronoiSurfaceRuntime {
public:
    VoronoiSurfaceRuntime() = default;
    ~VoronoiSurfaceRuntime();

    const std::vector<std::unique_ptr<VoronoiGeometryRuntime>>& getGeometryRuntimes() const { return geometryRuntimes; }

    bool initializeGeometryBindings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const RuntimeIntrinsicCache& intrinsicCache,
        const VoronoiPackage& voronoiPackage,
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes);
    void executeBufferTransfers(CommandPool& renderCommandPool);
    void cleanup();

private:
    std::vector<std::unique_ptr<VoronoiGeometryRuntime>> geometryRuntimes;
};
