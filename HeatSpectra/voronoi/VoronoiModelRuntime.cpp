#include "VoronoiModelRuntime.hpp"

#include "mesh/remesher/iODT.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "scene/Model.hpp"
#include "util/Structs.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <cstring>
#include <iostream>
#include <vector>

VoronoiModelRuntime::VoronoiModelRuntime(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    Model& model,
    const GeometryData& geometryData,
    const IntrinsicMeshData& intrinsicMeshData,
    const RuntimeIntrinsicCache::Entry& intrinsicResources,
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      model(model),
      nodeModelId(geometryData.modelId),
      runtimeModelId(model.getRuntimeModelId()),
      geometryData(geometryData),
      intrinsicMeshData(intrinsicMeshData),
      intrinsicResources(&intrinsicResources),
      renderCommandPool(renderCommandPool) {
}

VoronoiModelRuntime::~VoronoiModelRuntime() {
}

bool VoronoiModelRuntime::createVoronoiBuffers() {
    if (intrinsicMeshData.vertices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing intrinsic state for model" << std::endl;
        return false;
    }

    const size_t vertexCount = intrinsicMeshData.vertices.size();
    const size_t triangleCount = intrinsicMeshData.triangleIndices.size() / 3;
    if (vertexCount == 0) {
        std::cerr << "[VoronoiModelRuntime] Model has 0 intrinsic vertices" << std::endl;
        return false;
    }

    intrinsicVertexCount = vertexCount;
    intrinsicTriangleCount = triangleCount;
    intrinsicSurfacePositions.clear();
    intrinsicSurfacePositions.reserve(vertexCount);
    intrinsicTriangleIndices = intrinsicMeshData.triangleIndices;
    for (const auto& vertex : intrinsicMeshData.vertices) {
        intrinsicSurfacePositions.push_back(glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]));
    }

    constexpr uint32_t K_CANDIDATES = 64;
    const VkDeviceSize triangleIndicesBufferSize = sizeof(uint32_t) * intrinsicMeshData.triangleIndices.size();
    const VkDeviceSize candidateBufferSize = sizeof(uint32_t) * triangleCount * K_CANDIDATES;
    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    if (triangleIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
        triangleIndicesBuffer = VK_NULL_HANDLE;
        triangleIndicesBufferOffset = 0;
    }
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
    std::memcpy(triIdxStagingData, intrinsicMeshData.triangleIndices.data(), static_cast<size_t>(triangleIndicesBufferSize));

    const VkDeviceSize voronoiMappingSize = sizeof(VoronoiSurfaceMapping) * vertexCount;
    void* voronoiMappedPtr = nullptr;
    if (createStorageBuffer(
            memoryAllocator,
            vulkanDevice,
            nullptr,
            voronoiMappingSize,
            voronoiMappingBuffer,
            voronoiMappingBufferOffset,
            &voronoiMappedPtr,
            true,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT) != VK_SUCCESS) {
        std::cerr << "[VoronoiModelRuntime] Failed to create Voronoi mapping buffer" << std::endl;
        memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
        cleanup();
        return false;
    }

    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }

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

    std::cout << "[VoronoiModelRuntime] Created buffers for " << vertexCount << " vertices, "
              << triangleCount << " triangles" << std::endl;
    return true;
}

void VoronoiModelRuntime::stageVoronoiSurfaceMapping(const std::vector<uint32_t>& cellIndices) {
    if (cellIndices.empty()) {
        return;
    }
    if (intrinsicVertexCount != 0 && cellIndices.size() != intrinsicVertexCount) {
        return;
    }

    voronoiSurfaceCellIndices = cellIndices;

    std::vector<VoronoiSurfaceMapping> mappingData(cellIndices.size());
    for (size_t i = 0; i < cellIndices.size(); ++i) {
        mappingData[i].cellIndex = cellIndices[i];
    }

    const VkDeviceSize bufferSize = sizeof(VoronoiSurfaceMapping) * cellIndices.size();
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* stagingData = nullptr;
    if (createStagingBuffer(
            memoryAllocator,
            bufferSize,
            stagingBuffer,
            stagingOffset,
            &stagingData) != VK_SUCCESS ||
        !stagingData) {
        std::cerr << "[VoronoiModelRuntime] Failed to create Voronoi mapping staging buffer" << std::endl;
        return;
    }

    std::memcpy(stagingData, mappingData.data(), static_cast<size_t>(bufferSize));

    if (voronoiMappingStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingStagingBuffer, voronoiMappingStagingOffset);
        voronoiMappingStagingBuffer = VK_NULL_HANDLE;
        voronoiMappingStagingOffset = 0;
        voronoiMappingBufferSize = 0;
    }

    voronoiMappingStagingBuffer = stagingBuffer;
    voronoiMappingStagingOffset = stagingOffset;
    voronoiMappingBufferSize = bufferSize;

}

void VoronoiModelRuntime::executeBufferTransfers(VkCommandBuffer commandBuffer) {
    if (voronoiMappingStagingBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkBufferCopy mappingCopyRegion{ voronoiMappingStagingOffset, voronoiMappingBufferOffset, voronoiMappingBufferSize };
    vkCmdCopyBuffer(commandBuffer, voronoiMappingStagingBuffer, voronoiMappingBuffer, 1, &mappingCopyRegion);
}

void VoronoiModelRuntime::cleanup() {
    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    if (triangleIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
        triangleIndicesBuffer = VK_NULL_HANDLE;
        triangleIndicesBufferOffset = 0;
    }
    if (voronoiMappingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingBuffer, voronoiMappingBufferOffset);
        voronoiMappingBuffer = VK_NULL_HANDLE;
        voronoiMappingBufferOffset = 0;
    }
    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }

    cleanupStagingBuffers();
}

void VoronoiModelRuntime::cleanupStagingBuffers() {
    if (voronoiMappingStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingStagingBuffer, voronoiMappingStagingOffset);
        voronoiMappingStagingBuffer = VK_NULL_HANDLE;
        voronoiMappingStagingOffset = 0;
        voronoiMappingBufferSize = 0;
    }
}

VkBufferView VoronoiModelRuntime::getSupportingHalfedgeView() const {
    return intrinsicResources ? intrinsicResources->viewS : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getSupportingAngleView() const {
    return intrinsicResources ? intrinsicResources->viewA : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getHalfedgeView() const {
    return intrinsicResources ? intrinsicResources->viewH : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getEdgeView() const {
    return intrinsicResources ? intrinsicResources->viewE : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getTriangleView() const {
    return intrinsicResources ? intrinsicResources->viewT : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getLengthView() const {
    return intrinsicResources ? intrinsicResources->viewL : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getInputHalfedgeView() const {
    return intrinsicResources ? intrinsicResources->viewHInput : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getInputEdgeView() const {
    return intrinsicResources ? intrinsicResources->viewEInput : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getInputTriangleView() const {
    return intrinsicResources ? intrinsicResources->viewTInput : VK_NULL_HANDLE;
}

VkBufferView VoronoiModelRuntime::getInputLengthView() const {
    return intrinsicResources ? intrinsicResources->viewLInput : VK_NULL_HANDLE;
}
