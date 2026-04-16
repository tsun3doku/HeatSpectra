#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "mesh/remesher/Remesher.hpp"
#include "runtime/RuntimeProducts.hpp"

class ModelRegistry;
class VulkanDevice;

class RemeshSystem {
public:
    RemeshSystem(
        Remesher& remesher,
        VulkanDevice& vulkanDevice,
        ModelRegistry& resourceManager);
    ~RemeshSystem();

    void setSourceGeometry(const std::vector<float>& pointPositions, const std::vector<uint32_t>& triangleIndices);
    void setParams(int iterations, float minAngleDegrees, float maxEdgeLength, float stepSize);
    void setRuntimeModelId(uint32_t runtimeModelId);
    bool ensureConfigured();
    void disable();
    bool exportProduct(RemeshProduct& outProduct) const;
    bool isReady() const { return ready; }

private:
    void resetState(bool waitForDevice);

    VulkanDevice& vulkanDevice;
    ModelRegistry& resourceManager;
    Remesher& remesher;

    std::vector<float> pointPositions;
    std::vector<uint32_t> triangleIndices;
    int iterations = 1;
    float minAngleDegrees = 30.0f;
    float maxEdgeLength = 0.1f;
    float stepSize = 0.25f;
    uint32_t runtimeModelId = 0;
    std::vector<glm::vec3> geometryPositions;
    std::vector<uint32_t> geometryTriangleIndices;
    SupportingHalfedge::IntrinsicMesh intrinsicMesh;
    SupportingHalfedge::GPUResources intrinsicGpuResources;
    bool ready = false;
};
