#pragma once

#include "mesh/remesher/SupportingHalfedge.hpp"

#include <vector>

class VulkanDevice;
class MemoryAllocator;

struct RemeshResult {
    SupportingHalfedge::IntrinsicMesh intrinsicMesh;
    SupportingHalfedge::GPUResources intrinsicGpuResources;
};

class Remesher {
public:
    Remesher(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);

    bool remesh(
        const std::vector<float>& pointPositions,
        const std::vector<uint32_t>& triangleIndices,
        int iterations,
        double minAngleDegrees,
        double maxEdgeLength,
        double stepSize,
        RemeshResult& outResult) const;

private:
    SupportingHalfedge::GPUResources buildIntrinsicGPUResources(
        const SupportingHalfedge& supportingHalfedge,
        const SupportingHalfedge::IntrinsicMesh& intrinsicMesh) const;
    void cleanupGpuResources(SupportingHalfedge::GPUResources& resources) const;
    float computeAverageTriangleArea(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh) const;
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
};
