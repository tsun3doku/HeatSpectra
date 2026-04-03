#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "voronoi/VoronoiGeometryRuntime.hpp"

class CommandPool;
class MemoryAllocator;
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
        const std::vector<std::vector<VoronoiGeometryRuntime::SurfaceVertex>>& receiverSurfaceVertices,
        const std::vector<std::vector<uint32_t>>& receiverIntrinsicTriangleIndices,
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes);
    void executeBufferTransfers(CommandPool& renderCommandPool);
    void cleanup();

private:
    std::vector<std::unique_ptr<VoronoiGeometryRuntime>> geometryRuntimes;
};
