#include "VoronoiCandidate.hpp"

#include "VoronoiGpuStructs.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

namespace voronoi {

VoronoiCandidate::VoronoiCandidate(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), commandPool(commandPool) {
}

VoronoiCandidate::~VoronoiCandidate() {
    cleanup();
}

bool VoronoiCandidate::build(
    const std::vector<uint32_t>& inRuntimeModelIds,
    const std::vector<std::vector<glm::vec3>>& surfacePositions,
    const std::vector<std::vector<uint32_t>>& surfaceTriangleIndices) {
    cleanup();

    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    constexpr uint32_t kCandidatesPerFace = 64;

    runtimeModelIds.reserve(inRuntimeModelIds.size());
    faceCounts.reserve(inRuntimeModelIds.size());
    vertexBuffers.reserve(inRuntimeModelIds.size());
    vertexBufferOffsets.reserve(inRuntimeModelIds.size());
    faceIndexBuffers.reserve(inRuntimeModelIds.size());
    faceIndexBufferOffsets.reserve(inRuntimeModelIds.size());
    candidateBuffers.reserve(inRuntimeModelIds.size());
    candidateBufferOffsets.reserve(inRuntimeModelIds.size());

    for (size_t i = 0; i < inRuntimeModelIds.size(); ++i) {
        if (i >= surfaceTriangleIndices.size() || i >= surfacePositions.size()) {
            continue;
        }

        const uint32_t faceCount = static_cast<uint32_t>(surfaceTriangleIndices[i].size() / 3);
        if (faceCount == 0 || surfacePositions[i].empty()) {
            continue;
        }

        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexBufferOffset = 0;
        std::vector<voronoi::SurfaceVertex> vertices;
        vertices.reserve(surfacePositions[i].size());
        for (const glm::vec3& position : surfacePositions[i]) {
            vertices.push_back({glm::vec4(position, 1.0f), glm::vec4(0.0f)});
        }
        if (uploadDeviceBuffer(memoryAllocator, commandPool, vertices.data(),
                               vertices.size() * sizeof(voronoi::SurfaceVertex),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, alignment,
                               vertexBuffer, vertexBufferOffset) != VK_SUCCESS) {
            continue;
        }

        VkBuffer faceIndexBuffer = VK_NULL_HANDLE;
        VkDeviceSize faceIndexBufferOffset = 0;
        if (uploadDeviceBuffer(memoryAllocator, commandPool, surfaceTriangleIndices[i].data(),
                               surfaceTriangleIndices[i].size() * sizeof(uint32_t),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, alignment,
                               faceIndexBuffer, faceIndexBufferOffset) != VK_SUCCESS) {
            freeBuffer(memoryAllocator, vertexBuffer, vertexBufferOffset);
            continue;
        }

        VkBuffer candidateBuffer = VK_NULL_HANDLE;
        VkDeviceSize candidateBufferOffset = 0;
        std::vector<uint32_t> candidateInit(
            static_cast<size_t>(faceCount) * kCandidatesPerFace, 0xFFFFFFFFu);
        if (uploadDeviceBuffer(memoryAllocator, commandPool, candidateInit.data(),
                               candidateInit.size() * sizeof(uint32_t),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, alignment,
                               candidateBuffer, candidateBufferOffset) != VK_SUCCESS) {
            freeBuffer(memoryAllocator, vertexBuffer, vertexBufferOffset);
            freeBuffer(memoryAllocator, faceIndexBuffer, faceIndexBufferOffset);
            continue;
        }

        runtimeModelIds.push_back(inRuntimeModelIds[i]);
        faceCounts.push_back(faceCount);
        vertexBuffers.push_back(vertexBuffer);
        vertexBufferOffsets.push_back(vertexBufferOffset);
        faceIndexBuffers.push_back(faceIndexBuffer);
        faceIndexBufferOffsets.push_back(faceIndexBufferOffset);
        candidateBuffers.push_back(candidateBuffer);
        candidateBufferOffsets.push_back(candidateBufferOffset);
    }

    return true;
}

void VoronoiCandidate::cleanup() {
    for (size_t i = 0; i < vertexBuffers.size(); ++i) {
        if (vertexBuffers[i] != VK_NULL_HANDLE) {
            freeBuffer(memoryAllocator, vertexBuffers[i], vertexBufferOffsets[i]);
        }
    }
    for (size_t i = 0; i < faceIndexBuffers.size(); ++i) {
        if (faceIndexBuffers[i] != VK_NULL_HANDLE) {
            freeBuffer(memoryAllocator, faceIndexBuffers[i], faceIndexBufferOffsets[i]);
        }
    }
    for (size_t i = 0; i < candidateBuffers.size(); ++i) {
        if (candidateBuffers[i] != VK_NULL_HANDLE) {
            freeBuffer(memoryAllocator, candidateBuffers[i], candidateBufferOffsets[i]);
        }
    }

    runtimeModelIds.clear();
    faceCounts.clear();
    vertexBuffers.clear();
    vertexBufferOffsets.clear();
    faceIndexBuffers.clear();
    faceIndexBufferOffsets.clear();
    candidateBuffers.clear();
    candidateBufferOffsets.clear();
}

} // namespace voronoi
