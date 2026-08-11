#include "RemeshSystem.hpp"

#include "vulkan/VulkanDevice.hpp"

RemeshSystem::RemeshSystem(
    Remesher& remesher,
    VulkanDevice& vulkanDevice,
    ModelRegistry& modelRegistry)
    : vulkanDevice(vulkanDevice),
      modelRegistry(modelRegistry),
      remesher(remesher) {
}

RemeshSystem::~RemeshSystem() {
    disable();
}

void RemeshSystem::setSourceGeometry(const std::vector<float>& updatedPointPositions, const std::vector<uint32_t>& updatedTriangleIndices) {
    pointPositions = updatedPointPositions;
    triangleIndices = updatedTriangleIndices;
}

void RemeshSystem::setParams(int nextIterations, float nextMinAngleDegrees, float nextMaxEdgeLength, float nextStepSize) {
    iterations = nextIterations;
    minAngleDegrees = nextMinAngleDegrees;
    maxEdgeLength = nextMaxEdgeLength;
    stepSize = nextStepSize;
}

void RemeshSystem::setRuntimeModelId(uint32_t nextRuntimeModelId) {
    modelRuntimeId = nextRuntimeModelId;
}

bool RemeshSystem::remesh() {
    releaseOwnedGpuResources();
    mesh = {};

    if (!remesher.remesh(
            pointPositions,
            triangleIndices,
            iterations,
            minAngleDegrees,
            maxEdgeLength,
            stepSize,
            mesh,
            gpuResources)) {
        remesher.cleanupGpuResources(gpuResources);
        mesh = {};
        return false;
    }

    if (gpuResources.viewS == VK_NULL_HANDLE ||
        gpuResources.intrinsicTriangleBuffer == VK_NULL_HANDLE ||
        gpuResources.intrinsicVertexBuffer == VK_NULL_HANDLE) {
        remesher.cleanupGpuResources(gpuResources);
        mesh = {};
        return false;
    }

    return true;
}

SupportingHalfedge::GPUResources RemeshSystem::takeIntrinsicGpuResources() {
    SupportingHalfedge::GPUResources taken = gpuResources;
    gpuResources = {};
    return taken;
}

void RemeshSystem::disable() {
    releaseOwnedGpuResources();
    mesh = {};
}

void RemeshSystem::releaseOwnedGpuResources() {
    remesher.cleanupGpuResources(gpuResources);
}
