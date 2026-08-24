#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

namespace voronoi {

class VoronoiCandidate {
public:
    VoronoiCandidate(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool);
    ~VoronoiCandidate();

    VoronoiCandidate(const VoronoiCandidate&) = delete;
    VoronoiCandidate& operator=(const VoronoiCandidate&) = delete;

    bool build(
        const std::vector<uint32_t>& runtimeModelIds,
        const std::vector<std::vector<glm::vec3>>& surfacePositions,
        const std::vector<std::vector<uint32_t>>& surfaceTriangleIndices);

    void cleanup();

    size_t getModelCount() const { return runtimeModelIds.size(); }
    uint32_t getRuntimeModelId(size_t index) const { return index < runtimeModelIds.size() ? runtimeModelIds[index] : 0; }
    uint32_t getFaceCount(size_t index) const { return index < faceCounts.size() ? faceCounts[index] : 0; }

    VkBuffer getVertexBuffer(size_t index) const { return index < vertexBuffers.size() ? vertexBuffers[index] : VK_NULL_HANDLE; }
    VkDeviceSize getVertexBufferOffset(size_t index) const { return index < vertexBufferOffsets.size() ? vertexBufferOffsets[index] : 0; }

    VkBuffer getFaceIndexBuffer(size_t index) const { return index < faceIndexBuffers.size() ? faceIndexBuffers[index] : VK_NULL_HANDLE; }
    VkDeviceSize getFaceIndexBufferOffset(size_t index) const { return index < faceIndexBufferOffsets.size() ? faceIndexBufferOffsets[index] : 0; }

    VkBuffer getCandidateBuffer(size_t index) const { return index < candidateBuffers.size() ? candidateBuffers[index] : VK_NULL_HANDLE; }
    VkDeviceSize getCandidateBufferOffset(size_t index) const { return index < candidateBufferOffsets.size() ? candidateBufferOffsets[index] : 0; }

    const std::vector<uint32_t>& getRuntimeModelIds() const { return runtimeModelIds; }
    const std::vector<uint32_t>& getFaceCounts() const { return faceCounts; }
    const std::vector<VkBuffer>& getVertexBuffers() const { return vertexBuffers; }
    const std::vector<VkDeviceSize>& getVertexBufferOffsets() const { return vertexBufferOffsets; }
    const std::vector<VkBuffer>& getFaceIndexBuffers() const { return faceIndexBuffers; }
    const std::vector<VkDeviceSize>& getFaceIndexBufferOffsets() const { return faceIndexBufferOffsets; }
    const std::vector<VkBuffer>& getCandidateBuffers() const { return candidateBuffers; }
    const std::vector<VkDeviceSize>& getCandidateBufferOffsets() const { return candidateBufferOffsets; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;

    std::vector<uint32_t> runtimeModelIds;
    std::vector<uint32_t> faceCounts;
    std::vector<VkBuffer> vertexBuffers;
    std::vector<VkDeviceSize> vertexBufferOffsets;
    std::vector<VkBuffer> faceIndexBuffers;
    std::vector<VkDeviceSize> faceIndexBufferOffsets;
    std::vector<VkBuffer> candidateBuffers;
    std::vector<VkDeviceSize> candidateBufferOffsets;
};

} // namespace voronoi
