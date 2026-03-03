#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "scene/Model.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "util/Structs.hpp"
#include "HeatSource.hpp"
#include "util/GeometryUtils.hpp"

HeatSource::HeatSource(VulkanDevice& device, MemoryAllocator& allocator, Model& model, Remesher& remesher, CommandPool& cmdPool)
    : vulkanDevice(device), memoryAllocator(allocator), heatModel(model), remesher(remesher), renderCommandPool(cmdPool) {
    initialized = createSourceBuffer();
}

HeatSource::~HeatSource() {
    cleanup();
}

bool HeatSource::createSourceBuffer() {
    cleanup();
    initialized = false;

    intrinsicVertexCount = 0;
    triangleCount_ = 0;

    constexpr float kInitialTemperature = 100.0f;
    constexpr float kNormalEpsilon = 1e-12f;

    std::vector<SurfacePoint> surfacePoints;
    std::vector<uint32_t> indices;
    bool usingRemeshedData = false;

    iODT* iodt = remesher.getRemesherForModel(&heatModel);
    if (iodt) {
        SupportingHalfedge* supportingHalfedge = iodt->getSupportingHalfedge();
        if (supportingHalfedge) {
            auto intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
            if (!intrinsicMesh.vertices.empty() && intrinsicMesh.indices.size() >= 3) {
                intrinsicVertexCount = intrinsicMesh.vertices.size();
                indices = intrinsicMesh.indices;

                std::vector<glm::vec3> positions(intrinsicVertexCount);
                for (size_t i = 0; i < intrinsicVertexCount; ++i) {
                    positions[i] = intrinsicMesh.vertices[i].position;
                }
                const std::vector<float> vertexAreas = computeVertexAreas(positions, indices);

                surfacePoints.resize(intrinsicVertexCount);
                for (size_t i = 0; i < intrinsicVertexCount; ++i) {
                    surfacePoints[i].position = intrinsicMesh.vertices[i].position;
                    surfacePoints[i].temperature = kInitialTemperature;
                    surfacePoints[i].normal = intrinsicMesh.vertices[i].normal;
                    surfacePoints[i].area = (i < vertexAreas.size()) ? vertexAreas[i] : 0.0f;
                    surfacePoints[i].color = glm::vec4(1.0f);
                }

                usingRemeshedData = true;
                std::cout << "[HeatSource] Using remeshed data with " << intrinsicVertexCount << " vertices" << std::endl;
            }
        }
    }

    if (!usingRemeshedData) {
        const auto& modelVertices = heatModel.getVertices();
        const auto& modelIndices = heatModel.getIndices();

        if (modelVertices.empty() || modelIndices.size() < 3) {
            std::cerr << "[HeatSource] No usable geometry for source model; source buffers left empty" << std::endl;
            return false;
        }

        indices = modelIndices;

        std::vector<glm::vec3> positions(modelVertices.size());
        for (size_t i = 0; i < modelVertices.size(); ++i) {
            positions[i] = modelVertices[i].pos;
        }
        const std::vector<float> vertexAreas = computeVertexAreas(positions, indices);

        surfacePoints.resize(modelVertices.size());
        for (size_t i = 0; i < modelVertices.size(); ++i) {
            glm::vec3 normal = modelVertices[i].normal;
            const float normalLen2 = glm::dot(normal, normal);
            if (normalLen2 > kNormalEpsilon) {
                normal *= (1.0f / std::sqrt(normalLen2));
            } else {
                normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            surfacePoints[i].position = modelVertices[i].pos;
            surfacePoints[i].temperature = kInitialTemperature;
            surfacePoints[i].normal = normal;
            surfacePoints[i].area = (i < vertexAreas.size()) ? vertexAreas[i] : 0.0f;
            surfacePoints[i].color = glm::vec4(1.0f);
        }

        std::cout << "[HeatSource] Using fallback base mesh data with " << surfacePoints.size() << " vertices" << std::endl;
    }

    if (surfacePoints.empty()) {
        std::cerr << "[HeatSource] Surface point generation failed; source buffers left empty" << std::endl;
        return false;
    }

    const VkDeviceSize bufferSize = sizeof(SurfacePoint) * surfacePoints.size();

    auto [stagingBufferHandle, stagingBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (stagingBufferHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSource] Failed to allocate staging buffer for source points" << std::endl;
        cleanup();
        return false;
    }

    void* mapped = memoryAllocator.getMappedPointer(stagingBufferHandle, stagingBufferOffset);
    if (!mapped) {
        std::cerr << "[HeatSource] Failed to map staging buffer for source points" << std::endl;
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
        std::cerr << "[HeatSource] Failed to allocate device source buffer" << std::endl;
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
        std::cerr << "[HeatSource] Failed to create source buffer view" << std::endl;
        cleanup();
        return false;
    }

    VkCommandBuffer sourceCmd = renderCommandPool.beginCommands();
    if (sourceCmd == VK_NULL_HANDLE) {
        memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
        std::cerr << "[HeatSource] Failed to allocate command buffer for source upload" << std::endl;
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
        std::cerr << "[HeatSource] Source mesh has no triangles; skipping triangle buffers" << std::endl;
        initialized = true;
        return true;
    }

    std::vector<HeatSourceTriangleGPU> triangleData;
    std::vector<SurfacePoint> triangleCentroids;
    triangleData.reserve(trianglePrimitiveCount);
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
        const glm::vec3 normal = (crossLen > kNormalEpsilon) ? (cross / crossLen) : glm::vec3(0.0f, 1.0f, 0.0f);

        HeatSourceTriangleGPU triGpu{};
        triGpu.centerArea = glm::vec4(center, area);
        triGpu.normalPad = glm::vec4(normal, 0.0f);
        triGpu.indices = glm::uvec4(i0, i1, i2, 0u);
        triangleData.push_back(triGpu);

        SurfacePoint centroid{};
        centroid.position = center;
        centroid.temperature = kInitialTemperature;
        centroid.normal = normal;
        centroid.area = area;
        centroid.color = glm::vec4(1.0f);
        triangleCentroids.push_back(centroid);
    }

    triangleCount_ = static_cast<uint32_t>(triangleData.size());
    if (triangleData.empty()) {
        std::cerr << "[HeatSource] Triangle generation produced no valid triangles; skipping triangle buffers" << std::endl;
        initialized = true;
        return true;
    }

    const VkDeviceSize triBufferSize = sizeof(HeatSourceTriangleGPU) * triangleData.size();

    auto [triStagingHandle, triStagingOffset] = memoryAllocator.allocate(
        triBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (triStagingHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSource] Failed to allocate triangle staging buffer" << std::endl;
        initialized = true;
        return true;
    }

    void* triMapped = memoryAllocator.getMappedPointer(triStagingHandle, triStagingOffset);
    if (!triMapped) {
        std::cerr << "[HeatSource] Failed to map triangle staging buffer" << std::endl;
        memoryAllocator.free(triStagingHandle, triStagingOffset);
        initialized = true;
        return true;
    }
    std::memcpy(triMapped, triangleData.data(), static_cast<size_t>(triBufferSize));

    auto [triBufferHandle, triBufferOffset] = memoryAllocator.allocate(
        triBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (triBufferHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSource] Failed to allocate triangle GPU buffer" << std::endl;
        memoryAllocator.free(triStagingHandle, triStagingOffset);
        initialized = true;
        return true;
    }
    triangleGeometryBuffer = triBufferHandle;
    triangleGeometryBufferOffset_ = triBufferOffset;

    VkCommandBuffer triCmd = renderCommandPool.beginCommands();
    if (triCmd == VK_NULL_HANDLE) {
        std::cerr << "[HeatSource] Failed to allocate command buffer for triangle upload" << std::endl;
        memoryAllocator.free(triStagingHandle, triStagingOffset);
        memoryAllocator.free(triangleGeometryBuffer, triangleGeometryBufferOffset_);
        triangleGeometryBuffer = VK_NULL_HANDLE;
        triangleGeometryBufferOffset_ = 0;
        initialized = true;
        return true;
    }
    VkBufferCopy triRegion{};
    triRegion.srcOffset = triStagingOffset;
    triRegion.dstOffset = triangleGeometryBufferOffset_;
    triRegion.size = triBufferSize;
    vkCmdCopyBuffer(triCmd, triStagingHandle, triangleGeometryBuffer, 1, &triRegion);
    renderCommandPool.endCommands(triCmd);

    memoryAllocator.free(triStagingHandle, triStagingOffset);

    const VkDeviceSize centroidBufferSize = sizeof(SurfacePoint) * triangleCentroids.size();

    auto [centroidStagingHandle, centroidStagingOffset] = memoryAllocator.allocate(
        centroidBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (centroidStagingHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSource] Failed to allocate centroid staging buffer" << std::endl;
        initialized = true;
        return true;
    }
    void* centroidMapped = memoryAllocator.getMappedPointer(centroidStagingHandle, centroidStagingOffset);
    if (!centroidMapped) {
        std::cerr << "[HeatSource] Failed to map centroid staging buffer" << std::endl;
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
        std::cerr << "[HeatSource] Failed to allocate centroid GPU buffer" << std::endl;
        memoryAllocator.free(centroidStagingHandle, centroidStagingOffset);
        initialized = true;
        return true;
    }
    triangleCentroidBuffer = centroidBufferHandle;
    triangleCentroidBufferOffset_ = centroidBufferOffset;

    VkCommandBuffer centroidCmd = renderCommandPool.beginCommands();
    if (centroidCmd == VK_NULL_HANDLE) {
        std::cerr << "[HeatSource] Failed to allocate command buffer for centroid upload" << std::endl;
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

size_t HeatSource::getVertexCount() const {
    return intrinsicVertexCount > 0 ? intrinsicVertexCount : heatModel.getVertexCount();
}

void HeatSource::cleanup() {
    if (sourceBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), sourceBufferView, nullptr);
        sourceBufferView = VK_NULL_HANDLE;
    }
    if (sourceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(sourceBuffer, sourceBufferOffset_);
        sourceBuffer = VK_NULL_HANDLE;
    }
    sourceBufferOffset_ = 0;

    if (triangleGeometryBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleGeometryBuffer, triangleGeometryBufferOffset_);
        triangleGeometryBuffer = VK_NULL_HANDLE;
    }
    triangleGeometryBufferOffset_ = 0;

    if (triangleCentroidBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset_);
        triangleCentroidBuffer = VK_NULL_HANDLE;
    }
    triangleCentroidBufferOffset_ = 0;
    triangleCount_ = 0;
    intrinsicVertexCount = 0;
    initialized = false;
}
