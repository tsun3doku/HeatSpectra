#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "mesh/remesher/Remesher.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"

class ModelRegistry;
class VulkanDevice;

// RemeshSystem is a product-agnostic backend: it runs a remesh and owns the
// resulting CPU mesh + GPU resources until the controller takes them. It knows
// nothing about RemeshProduct - the controller assembles the product.
class RemeshSystem {
public:
    RemeshSystem(
        Remesher& remesher,
        VulkanDevice& vulkanDevice,
        ModelRegistry& modelRegistry);
    ~RemeshSystem();

    void setSourceGeometry(const std::vector<float>& pointPositions, const std::vector<uint32_t>& triangleIndices);
    void setParams(int iterations, float minAngleDegrees, float maxEdgeLength, float stepSize);
    void setRuntimeModelId(uint32_t runtimeModelId);

    // Run the remesh into internal state. Frees any previously-owned GPU
    // resources from a prior remesh that were never taken. Returns false on
    // failure (internal state is cleared).
    bool remesh();

    // CPU data: borrowed by const ref (controller copies by value into product).
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh() const { return mesh; }
    const std::vector<float>& sourcePositions() const { return pointPositions; }
    const std::vector<uint32_t>& sourceTriangles() const { return triangleIndices; }
    uint32_t runtimeModelId() const { return modelRuntimeId; }

    // GPU resources: ownership transfers OUT to the caller. System's copy is
    // zeroed, so destructor/disable will not double-free.
    SupportingHalfedge::GPUResources takeIntrinsicGpuResources();

    void disable();

private:
    void releaseOwnedGpuResources();

    VulkanDevice& vulkanDevice;
    ModelRegistry& modelRegistry;
    Remesher& remesher;

    std::vector<float> pointPositions;
    std::vector<uint32_t> triangleIndices;
    int iterations = 1;
    float minAngleDegrees = 20.0f;
    float maxEdgeLength = 0.1f;
    float stepSize = 0.25f;
    uint32_t modelRuntimeId = 0;

    SupportingHalfedge::IntrinsicMesh mesh;
    SupportingHalfedge::GPUResources gpuResources{};
};
