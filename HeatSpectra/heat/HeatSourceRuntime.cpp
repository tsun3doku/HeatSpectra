#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "HeatSourceRuntime.hpp"
#include "util/GeometryUtils.hpp"

HeatSourceRuntime::HeatSourceRuntime(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
    CommandPool& cmdPool,
    float initialTemperatureValue)
    : vulkanDevice(device),
      memoryAllocator(allocator),
      intrinsicMesh(intrinsicMesh),
      renderCommandPool(cmdPool),
      uniformTemperature(initialTemperatureValue) {
    initialized = createSourceBuffer();
}

HeatSourceRuntime::~HeatSourceRuntime() {
    cleanup();
}

bool HeatSourceRuntime::createSourceBuffer() {
    cleanup();
    initialized = false;

    intrinsicVertexCount = 0;
    triangleCount_ = 0;

    const float initialTemperatureValue = uniformTemperature;
    constexpr float normalEpsilon = 1e-12f;

    std::vector<heat::SurfacePoint> surfacePoints;
    std::vector<uint32_t> indices;
    if (intrinsicMesh.vertices.empty() || intrinsicMesh.indices.size() < 3) {
        std::cerr << "[HeatSourceRuntime] Missing intrinsic payload for source model; source buffers left empty" << std::endl;
        return false;
    }

    intrinsicVertexCount = intrinsicMesh.vertices.size();
    indices = intrinsicMesh.indices;

    std::vector<glm::vec3> positions(intrinsicVertexCount);
    for (size_t i = 0; i < intrinsicVertexCount; ++i) {
        positions[i] = intrinsicMesh.vertices[i].position;
    }
    const std::vector<float> vertexAreas = computeVertexAreas(positions, indices);

    surfacePoints.resize(intrinsicVertexCount);
    for (size_t i = 0; i < intrinsicVertexCount; ++i) {
        surfacePoints[i].position = positions[i];
        surfacePoints[i].temperature = initialTemperatureValue;
        surfacePoints[i].normal = intrinsicMesh.vertices[i].normal;
        surfacePoints[i].area = (i < vertexAreas.size()) ? vertexAreas[i] : 0.0f;
        surfacePoints[i].color = glm::vec4(1.0f);
    }

    if (surfacePoints.empty()) {
        std::cerr << "[HeatSourceRuntime] Surface point generation failed; source buffers left empty" << std::endl;
        return false;
    }

    surfacePointsCache = surfacePoints;

    const VkDeviceSize bufferSize = sizeof(heat::SurfacePoint) * surfacePoints.size();

    auto [stagingBufferHandle, stagingBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (stagingBufferHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSourceRuntime] Failed to allocate staging buffer for source points" << std::endl;
        cleanup();
        return false;
    }

    void* mapped = memoryAllocator.getMappedPointer(stagingBufferHandle, stagingBufferOffset);
    if (!mapped) {
        std::cerr << "[HeatSourceRuntime] Failed to map staging buffer for source points" << std::endl;
        memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
        cleanup();
        return false;
    }
    std::memcpy(mapped, surfacePoints.data(), static_cast<size_t>(bufferSize));

    auto [sourceBufferHandle, sourceBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (sourceBufferHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSourceRuntime] Failed to allocate device source buffer" << std::endl;
        memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
        cleanup();
        return false;
    }
    sourceBuffer = sourceBufferHandle;
    sourceBufferOffset_ = sourceBufferOffset;

    VkBufferViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewInfo.buffer = sourceBuffer;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.offset = sourceBufferOffset_;
    viewInfo.range = bufferSize;

    if (vkCreateBufferView(vulkanDevice.getDevice(), &viewInfo, nullptr, &sourceBufferView) != VK_SUCCESS) {
        memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
        std::cerr << "[HeatSourceRuntime] Failed to create source buffer view" << std::endl;
        cleanup();
        return false;
    }

    VkCommandBuffer sourceCmd = renderCommandPool.beginCommands();
    if (sourceCmd == VK_NULL_HANDLE) {
        memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
        std::cerr << "[HeatSourceRuntime] Failed to allocate command buffer for source upload" << std::endl;
        cleanup();
        return false;
    }
    VkBufferCopy sourceRegion{};
    sourceRegion.srcOffset = stagingBufferOffset;
    sourceRegion.dstOffset = sourceBufferOffset_;
    sourceRegion.size = bufferSize;
    vkCmdCopyBuffer(sourceCmd, stagingBufferHandle, sourceBuffer, 1, &sourceRegion);
    renderCommandPool.endCommands(sourceCmd);

    memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);

    const size_t trianglePrimitiveCount = indices.size() / 3;
    if (trianglePrimitiveCount == 0) {
        std::cerr << "[HeatSourceRuntime] Source mesh has no triangles; skipping triangle buffers" << std::endl;
        initialized = true;
        return true;
    }

    std::vector<heat::SurfacePoint> triangleCentroids;
    triangleCentroids.reserve(trianglePrimitiveCount);

    for (size_t triangleIndex = 0; triangleIndex < trianglePrimitiveCount; ++triangleIndex) {
        const uint32_t i0 = indices[triangleIndex * 3 + 0];
        const uint32_t i1 = indices[triangleIndex * 3 + 1];
        const uint32_t i2 = indices[triangleIndex * 3 + 2];

        if (i0 >= surfacePoints.size() || i1 >= surfacePoints.size() || i2 >= surfacePoints.size()) {
            continue;
        }

        const glm::vec3 v0 = glm::vec3(surfacePoints[i0].position);
        const glm::vec3 v1 = glm::vec3(surfacePoints[i1].position);
        const glm::vec3 v2 = glm::vec3(surfacePoints[i2].position);

        const glm::vec3 center = (v0 + v1 + v2) / 3.0f;
        const glm::vec3 edge1 = v1 - v0;
        const glm::vec3 edge2 = v2 - v0;
        const glm::vec3 cross = glm::cross(edge1, edge2);
        const float crossLen = glm::length(cross);
        const float area = 0.5f * crossLen;
        const glm::vec3 normal = (crossLen > normalEpsilon) ? (cross / crossLen) : glm::vec3(0.0f, 1.0f, 0.0f);

        heat::SurfacePoint centroid{};
        centroid.position = center;
        centroid.temperature = initialTemperatureValue;
        centroid.normal = normal;
        centroid.area = area;
        centroid.color = glm::vec4(1.0f);
        triangleCentroids.push_back(centroid);
    }

    triangleCount_ = static_cast<uint32_t>(triangleCentroids.size());
    if (triangleCentroids.empty()) {
        std::cerr << "[HeatSourceRuntime] Triangle generation produced no valid triangles; skipping triangle buffers" << std::endl;
        initialized = true;
        return true;
    }

    triangleCentroidsCache = triangleCentroids;

    const VkDeviceSize centroidBufferSize = sizeof(heat::SurfacePoint) * triangleCentroids.size();

    auto [centroidStagingHandle, centroidStagingOffset] = memoryAllocator.allocate(
        centroidBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (centroidStagingHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSourceRuntime] Failed to allocate centroid staging buffer" << std::endl;
        initialized = true;
        return true;
    }
    void* centroidMapped = memoryAllocator.getMappedPointer(centroidStagingHandle, centroidStagingOffset);
    if (!centroidMapped) {
        std::cerr << "[HeatSourceRuntime] Failed to map centroid staging buffer" << std::endl;
        memoryAllocator.free(centroidStagingHandle, centroidStagingOffset);
        initialized = true;
        return true;
    }
    std::memcpy(centroidMapped, triangleCentroids.data(), static_cast<size_t>(centroidBufferSize));

    auto [centroidBufferHandle, centroidBufferOffset] = memoryAllocator.allocate(
        centroidBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (centroidBufferHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSourceRuntime] Failed to allocate centroid GPU buffer" << std::endl;
        memoryAllocator.free(centroidStagingHandle, centroidStagingOffset);
        initialized = true;
        return true;
    }
    triangleCentroidBuffer = centroidBufferHandle;
    triangleCentroidBufferOffset_ = centroidBufferOffset;

    VkCommandBuffer centroidCmd = renderCommandPool.beginCommands();
    if (centroidCmd == VK_NULL_HANDLE) {
        std::cerr << "[HeatSourceRuntime] Failed to allocate command buffer for centroid upload" << std::endl;
        memoryAllocator.free(centroidStagingHandle, centroidStagingOffset);
        memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset_);
        triangleCentroidBuffer = VK_NULL_HANDLE;
        triangleCentroidBufferOffset_ = 0;
        initialized = true;
        return true;
    }
    VkBufferCopy centroidRegion{};
    centroidRegion.srcOffset = centroidStagingOffset;
    centroidRegion.dstOffset = triangleCentroidBufferOffset_;
    centroidRegion.size = centroidBufferSize;
    vkCmdCopyBuffer(centroidCmd, centroidStagingHandle, triangleCentroidBuffer, 1, &centroidRegion);
    renderCommandPool.endCommands(centroidCmd);

    memoryAllocator.free(centroidStagingHandle, centroidStagingOffset);
    initialized = true;
    return true;
}

size_t HeatSourceRuntime::getVertexCount() const {
    return intrinsicVertexCount;
}

void HeatSourceRuntime::cleanup() {
    if (sourceBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), sourceBufferView, nullptr);
        sourceBufferView = VK_NULL_HANDLE;
    }
    if (sourceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(sourceBuffer, sourceBufferOffset_);
        sourceBuffer = VK_NULL_HANDLE;
    }
    sourceBufferOffset_ = 0;

    if (triangleCentroidBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset_);
        triangleCentroidBuffer = VK_NULL_HANDLE;
    }
    triangleCentroidBufferOffset_ = 0;
    triangleCount_ = 0;
    intrinsicVertexCount = 0;
    initialized = false;
    surfacePointsCache.clear();
    triangleCentroidsCache.clear();
}

void HeatSourceRuntime::setUniformTemperature(float temperature) {
    uniformTemperature = temperature;
}
