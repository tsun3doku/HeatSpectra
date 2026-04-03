#include "VoronoiModelRuntime.hpp"

#include "mesh/remesher/iODT.hpp"
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
    uint32_t runtimeModelId,
    VkBuffer vertexBuffer,
    VkDeviceSize vertexBufferOffset,
    VkBuffer indexBuffer,
    VkDeviceSize indexBufferOffset,
    uint32_t indexCount,
    const glm::mat4& modelMatrix,
    CpuData cpuData,
    VkBufferView supportingHalfedgeView,
    VkBufferView supportingAngleView,
    VkBufferView halfedgeView,
    VkBufferView edgeView,
    VkBufferView triangleView,
    VkBufferView lengthView,
    VkBufferView inputHalfedgeView,
    VkBufferView inputEdgeView,
    VkBufferView inputTriangleView,
    VkBufferView inputLengthView,
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      nodeModelId(cpuData.nodeModelId),
      runtimeModelId(runtimeModelId),
      vertexBuffer(vertexBuffer),
      vertexBufferOffset(vertexBufferOffset),
      indexBuffer(indexBuffer),
      indexBufferOffset(indexBufferOffset),
      indexCount(indexCount),
      modelMatrix(modelMatrix),
      intrinsicMesh(std::move(cpuData.intrinsicMesh)),
      geometryPositions(std::move(cpuData.geometryPositions)),
      geometryTriangleIndices(std::move(cpuData.geometryTriangleIndices)),
      intrinsicSurfacePositions(std::move(cpuData.intrinsicSurfacePositions)),
      intrinsicTriangleIndices(std::move(cpuData.intrinsicTriangleIndices)),
      supportingHalfedgeView(supportingHalfedgeView),
      supportingAngleView(supportingAngleView),
      halfedgeView(halfedgeView),
      edgeView(edgeView),
      triangleView(triangleView),
      lengthView(lengthView),
      inputHalfedgeView(inputHalfedgeView),
      inputEdgeView(inputEdgeView),
      inputTriangleView(inputTriangleView),
      inputLengthView(inputLengthView),
      renderCommandPool(renderCommandPool) {
}

VoronoiModelRuntime::~VoronoiModelRuntime() {
}

bool VoronoiModelRuntime::createVoronoiBuffers() {
    if (intrinsicMesh.vertices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing intrinsic state for model" << std::endl;
        return false;
    }

    const size_t vertexCount = intrinsicMesh.vertices.size();
    const size_t triangleCount = intrinsicMesh.indices.size() / 3;
    if (vertexCount == 0) {
        std::cerr << "[VoronoiModelRuntime] Model has 0 intrinsic vertices" << std::endl;
        return false;
    }

    intrinsicVertexCount = vertexCount;
    intrinsicTriangleCount = triangleCount;

    constexpr uint32_t K_CANDIDATES = 64;
    const VkDeviceSize triangleIndicesBufferSize = sizeof(uint32_t) * intrinsicTriangleIndices.size();
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
    std::memcpy(triIdxStagingData, intrinsicTriangleIndices.data(), static_cast<size_t>(triangleIndicesBufferSize));

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
    return supportingHalfedgeView;
}

VkBufferView VoronoiModelRuntime::getSupportingAngleView() const {
    return supportingAngleView;
}

VkBufferView VoronoiModelRuntime::getHalfedgeView() const {
    return halfedgeView;
}

VkBufferView VoronoiModelRuntime::getEdgeView() const {
    return edgeView;
}

VkBufferView VoronoiModelRuntime::getTriangleView() const {
    return triangleView;
}

VkBufferView VoronoiModelRuntime::getLengthView() const {
    return lengthView;
}

VkBufferView VoronoiModelRuntime::getInputHalfedgeView() const {
    return inputHalfedgeView;
}

VkBufferView VoronoiModelRuntime::getInputEdgeView() const {
    return inputEdgeView;
}

VkBufferView VoronoiModelRuntime::getInputTriangleView() const {
    return inputTriangleView;
}

VkBufferView VoronoiModelRuntime::getInputLengthView() const {
    return inputLengthView;
}
