#include "VoronoiModelRuntime.hpp"

#include "mesh/remesher/iODT.hpp"
#include "util/Structs.hpp"
#include "voronoi/VoronoiNodeDomain.hpp"
#include "spatial/VoxelGrid.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

VoronoiModelRuntime::VoronoiModelRuntime(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    uint32_t runtimeModelId,
    const glm::mat4& modelMatrix,
    const std::vector<glm::vec3>& geometryPositions,
    const std::vector<uint32_t>& geometryTriangleIndices,
    const std::vector<voronoi::SurfaceVertex>& surfaceVertices,
    const std::vector<uint32_t>& surfaceTriangleIndices,
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      runtimeModelId(runtimeModelId),
      modelMatrix(modelMatrix),
      geometryPositions(geometryPositions),
      geometryTriangleIndices(geometryTriangleIndices),
      surfaceVertices(surfaceVertices),
      surfaceTriangleIndices(surfaceTriangleIndices),
      renderCommandPool(renderCommandPool) {
}

VoronoiModelRuntime::~VoronoiModelRuntime() = default;

bool VoronoiModelRuntime::createVoronoiBuffers() {
    if (surfaceVertices.empty() || surfaceTriangleIndices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing surface geometry for model" << std::endl;
        return false;
    }

    const size_t vertexCount = surfaceVertices.size();
    const size_t triangleCount = surfaceTriangleIndices.size() / 3;
    if (vertexCount == 0) {
        std::cerr << "[VoronoiModelRuntime] Model has 0 intrinsic vertices" << std::endl;
        return false;
    }

    constexpr uint32_t K_CANDIDATES = 64;
    const VkDeviceSize triangleIndicesBufferSize = sizeof(uint32_t) * surfaceTriangleIndices.size();
    const VkDeviceSize candidateBufferSize = sizeof(uint32_t) * triangleCount * K_CANDIDATES;
    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    freeBuffer(memoryAllocator, triangleIndicesBuffer, triangleIndicesBufferOffset);
    auto [triIdxHandle, triIdxOffset] = memoryAllocator.allocate(
        triangleIndicesBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        storageAlignment);
    if (triIdxHandle == VK_NULL_HANDLE) {
        std::cerr << "[VoronoiModelRuntime] Failed to allocate triangle index buffer" << std::endl;
        cleanup();
        return false;
    }
    triangleIndicesBuffer = triIdxHandle;
    triangleIndicesBufferOffset = triIdxOffset;

    VkBuffer triIdxStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize triIdxStagingOffset = 0;
    void* triIdxStagingData = nullptr;
    if (createStagingBuffer(
            memoryAllocator,
            triangleIndicesBufferSize,
            triIdxStagingBuffer,
            triIdxStagingOffset,
            &triIdxStagingData) != VK_SUCCESS ||
        !triIdxStagingData) {
        std::cerr << "[VoronoiModelRuntime] Failed to create triangle index staging buffer" << std::endl;
        cleanup();
        return false;
    }
    std::memcpy(triIdxStagingData, surfaceTriangleIndices.data(), static_cast<size_t>(triangleIndicesBufferSize));

    freeBuffer(memoryAllocator, voronoiCandidateBuffer, voronoiCandidateBufferOffset);

    VkBuffer candidateStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize candidateStagingOffset = 0;
    if (candidateBufferSize > 0) {
        auto [candidateHandle, candidateOffset] = memoryAllocator.allocate(
            candidateBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            storageAlignment);
        if (candidateHandle == VK_NULL_HANDLE) {
            std::cerr << "[VoronoiModelRuntime] Failed to allocate Voronoi candidate buffer" << std::endl;
            memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
            cleanup();
            return false;
        }
        voronoiCandidateBuffer = candidateHandle;
        voronoiCandidateBufferOffset = candidateOffset;

        std::vector<uint32_t> candidateInitData(static_cast<size_t>(triangleCount) * K_CANDIDATES, 0xFFFFFFFFu);
        void* candidateStagingData = nullptr;
        if (createStagingBuffer(
                memoryAllocator,
                candidateBufferSize,
                candidateStagingBuffer,
                candidateStagingOffset,
                &candidateStagingData) != VK_SUCCESS ||
            !candidateStagingData) {
            std::cerr << "[VoronoiModelRuntime] Failed to create Voronoi candidate staging buffer" << std::endl;
            memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
            cleanup();
            return false;
        }
        std::memcpy(candidateStagingData, candidateInitData.data(), static_cast<size_t>(candidateBufferSize));
    }

    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    {
        VkBufferCopy region{};
        region.srcOffset = triIdxStagingOffset;
        region.dstOffset = triangleIndicesBufferOffset;
        region.size = triangleIndicesBufferSize;
        vkCmdCopyBuffer(cmd, triIdxStagingBuffer, triangleIndicesBuffer, 1, &region);
    }
    if (candidateStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy region{};
        region.srcOffset = candidateStagingOffset;
        region.dstOffset = voronoiCandidateBufferOffset;
        region.size = candidateBufferSize;
        vkCmdCopyBuffer(cmd, candidateStagingBuffer, voronoiCandidateBuffer, 1, &region);
    }
    renderCommandPool.endCommands(cmd);

    memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
    if (candidateStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(candidateStagingBuffer, candidateStagingOffset);
    }

    return true;
}

bool VoronoiModelRuntime::createSurfaceBuffers() {
    if (surfaceVertices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing surface state for model" << std::endl;
        return false;
    }
    const size_t vertexCount = surfaceVertices.size();
    if (vertexCount == 0) {
        std::cerr << "[VoronoiModelRuntime] Model has 0 surface vertices" << std::endl;
        return false;
    }

    freeBuffer(memoryAllocator, surfaceBuffer, surfaceBufferOffset);

    const VkDeviceSize vertexBufferSize = sizeof(voronoi::SurfaceVertex) * vertexCount;
    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    if (uploadDeviceBuffer(
            memoryAllocator,
            renderCommandPool,
            surfaceVertices.data(),
            vertexBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            storageAlignment,
            surfaceBuffer,
            surfaceBufferOffset) != VK_SUCCESS) {
        std::cerr << "[VoronoiModelRuntime] Failed to create surface vertex buffer" << std::endl;
        cleanup();
        return false;
    }
    return true;
}

bool VoronoiModelRuntime::resetSurfaceState() {
    return createSurfaceBuffers();
}

bool VoronoiModelRuntime::stageGMLSSurfaceData(
    const std::vector<voronoi::GMLSSurfaceStencil>& stencils,
    const std::vector<voronoi::GMLSSurfaceWeight>& valueWeights,
    const std::vector<voronoi::GMLSSurfaceGradientWeight>& gradientWeights) {
    if (stencils.empty() || surfaceVertices.empty() || stencils.size() != surfaceVertices.size()) {
        return false;
    }

    freeBuffer(memoryAllocator, gmlsSurfaceStencilBuffer, gmlsSurfaceStencilBufferOffset);
    freeBuffer(memoryAllocator, gmlsSurfaceWeightBuffer, gmlsSurfaceWeightBufferOffset);
    freeBuffer(memoryAllocator, gmlsSurfaceGradientWeightBuffer, gmlsSurfaceGradientWeightBufferOffset);

    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    {
        VkDeviceSize size = sizeof(voronoi::GMLSSurfaceStencil) * stencils.size();
        if (uploadDeviceBuffer(
                memoryAllocator,
                renderCommandPool,
                stencils.data(),
                size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                storageAlignment,
                gmlsSurfaceStencilBuffer,
                gmlsSurfaceStencilBufferOffset) != VK_SUCCESS) {
            return false;
        }
    }

    {
        VkDeviceSize size = sizeof(voronoi::GMLSSurfaceWeight) * valueWeights.size();
        if (uploadDeviceBuffer(
                memoryAllocator,
                renderCommandPool,
                valueWeights.data(),
                size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                storageAlignment,
                gmlsSurfaceWeightBuffer,
                gmlsSurfaceWeightBufferOffset) != VK_SUCCESS) {
            return false;
        }
        valueWeightCount = valueWeights.size();
    }

    {
        VkDeviceSize size = sizeof(voronoi::GMLSSurfaceGradientWeight) * gradientWeights.size();
        if (uploadDeviceBuffer(
                memoryAllocator,
                renderCommandPool,
                gradientWeights.data(),
                size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                storageAlignment,
                gmlsSurfaceGradientWeightBuffer,
                gmlsSurfaceGradientWeightBufferOffset) != VK_SUCCESS) {
            return false;
        }
        gradientWeightCount = gradientWeights.size();
    }
    return true;
}

void VoronoiModelRuntime::cleanup() {
    // Product buffers owned by published VoronoiProduct 
    voronoiCandidateBuffer = VK_NULL_HANDLE;
    voronoiCandidateBufferOffset = 0;
    gmlsSurfaceStencilBuffer = VK_NULL_HANDLE;
    gmlsSurfaceStencilBufferOffset = 0;
    gmlsSurfaceWeightBuffer = VK_NULL_HANDLE;
    gmlsSurfaceWeightBufferOffset = 0;
    gmlsSurfaceGradientWeightBuffer = VK_NULL_HANDLE;
    gmlsSurfaceGradientWeightBufferOffset = 0;

    // Internal buffers not part of any product
    freeBuffer(memoryAllocator, triangleIndicesBuffer, triangleIndicesBufferOffset);
    freeBuffer(memoryAllocator, surfaceBuffer, surfaceBufferOffset);
}

bool VoronoiModelRuntime::buildAndStageSurfaceMappings(
    VoronoiNodeDomain& nodeDomain, const VoxelGrid& voxelGrid) {
    if (surfaceVertices.empty()) return false;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    positions.reserve(surfaceVertices.size());
    normals.reserve(surfaceVertices.size());
    for (const voronoi::SurfaceVertex& vertex : surfaceVertices) {
        positions.push_back(glm::vec3(vertex.position));
        normals.push_back(glm::vec3(vertex.normal));
    }

    if (!nodeDomain.buildSurfaceMappings(positions, normals, voxelGrid)) return false;
    return stageGMLSSurfaceData(
        nodeDomain.getSurfaceStencils(),
        nodeDomain.getSurfaceValueWeights(),
        nodeDomain.getSurfaceGradientWeights());
}
