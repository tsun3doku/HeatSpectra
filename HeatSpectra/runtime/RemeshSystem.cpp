#include "RemeshSystem.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"

namespace {

std::vector<glm::vec3> buildGeometryPositions(const std::vector<float>& pointPositions) {
    std::vector<glm::vec3> positions;
    positions.reserve(pointPositions.size() / 3);
    for (size_t index = 0; index + 2 < pointPositions.size(); index += 3) {
        positions.emplace_back(
            pointPositions[index],
            pointPositions[index + 1],
            pointPositions[index + 2]);
    }
    return positions;
}

} // namespace

RemeshSystem::RemeshSystem(
    Remesher& remesher,
    VulkanDevice& vulkanDevice,
    ModelRegistry& resourceManager)
    : vulkanDevice(vulkanDevice),
      resourceManager(resourceManager),
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
    runtimeModelId = nextRuntimeModelId;
}

bool RemeshSystem::ensureConfigured() {
    resetState(true);

    RemeshResult remeshResult{};
    if (!remesher.remesh(
            pointPositions,
            triangleIndices,
            iterations,
            minAngleDegrees,
            maxEdgeLength,
            stepSize,
            remeshResult)) {
        return false;
    }

    if (remeshResult.intrinsicGpuResources.viewS == VK_NULL_HANDLE ||
        remeshResult.intrinsicGpuResources.intrinsicTriangleBuffer == VK_NULL_HANDLE ||
        remeshResult.intrinsicGpuResources.intrinsicVertexBuffer == VK_NULL_HANDLE) {
        remesher.cleanupGpuResources(remeshResult.intrinsicGpuResources);
        return false;
    }

    geometryPositions = buildGeometryPositions(pointPositions);
    geometryTriangleIndices = triangleIndices;
    intrinsicMesh = remeshResult.intrinsicMesh;
    intrinsicGpuResources = remeshResult.intrinsicGpuResources;
    ready = true;
    return true;
}

void RemeshSystem::disable() {
    resetState(true);
}

bool RemeshSystem::exportProduct(RemeshProduct& outProduct) const {
    outProduct = {};

    if (!ready) {
        return false;
    }

    outProduct.runtimeModelId = runtimeModelId;
    outProduct.geometryPositions = geometryPositions;
    outProduct.geometryTriangleIndices = geometryTriangleIndices;
    outProduct.intrinsicMesh = intrinsicMesh;
    outProduct.intrinsicTriangleBuffer = intrinsicGpuResources.intrinsicTriangleBuffer;
    outProduct.intrinsicTriangleBufferOffset = intrinsicGpuResources.triangleGeometryOffset;
    outProduct.intrinsicVertexBuffer = intrinsicGpuResources.intrinsicVertexBuffer;
    outProduct.intrinsicVertexBufferOffset = intrinsicGpuResources.vertexGeometryOffset;
    outProduct.intrinsicTriangleCount = intrinsicGpuResources.triangleCount;
    outProduct.intrinsicVertexCount = intrinsicGpuResources.vertexCount;
    outProduct.averageTriangleArea = intrinsicGpuResources.averageTriangleArea;
    outProduct.supportingHalfedgeView = intrinsicGpuResources.viewS;
    outProduct.supportingAngleView = intrinsicGpuResources.viewA;
    outProduct.halfedgeView = intrinsicGpuResources.viewH;
    outProduct.edgeView = intrinsicGpuResources.viewE;
    outProduct.triangleView = intrinsicGpuResources.viewT;
    outProduct.lengthView = intrinsicGpuResources.viewL;
    outProduct.inputHalfedgeView = intrinsicGpuResources.viewHInput;
    outProduct.inputEdgeView = intrinsicGpuResources.viewEInput;
    outProduct.inputTriangleView = intrinsicGpuResources.viewTInput;
    outProduct.inputLengthView = intrinsicGpuResources.viewLInput;
    outProduct.productHash = buildProductHash(outProduct);
    return outProduct.isValid();
}

void RemeshSystem::resetState(bool waitForDevice) {
    if (waitForDevice && ready) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

    remesher.cleanupGpuResources(intrinsicGpuResources);
    geometryPositions.clear();
    geometryTriangleIndices.clear();
    intrinsicMesh = {};
    ready = false;
}
