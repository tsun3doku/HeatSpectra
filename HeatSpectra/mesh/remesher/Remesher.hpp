#pragma once

#include "domain/RemeshData.hpp"

class VulkanDevice;
class MemoryAllocator;

class Remesher {
public:
    Remesher(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);

    bool remesh(
        const GeometryData& inputGeometry,
        int iterations,
        double minAngleDegrees,
        double maxEdgeLength,
        double stepSize,
        RemeshResultData& outResult) const;

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
};
