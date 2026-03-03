#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "util/Structs.hpp"

class VulkanDevice;
class MemoryAllocator;
class Model;
class CommandPool;
class Remesher;

class HeatReceiver {
public:
    HeatReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,Model& receiverModel,
                 Remesher& remesher, CommandPool& renderCommandPool);
    ~HeatReceiver();

    bool createReceiverBuffers();
    bool initializeReceiverBuffer();

    void stageVoronoiSurfaceMapping(const std::vector<uint32_t>& cellIndices);

    void updateDescriptors(
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorPool surfacePool,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t nodeCount);
    
    void executeBufferTransfers(VkCommandBuffer commandBuffer);
    
    void recreateDescriptors(
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorPool surfacePool,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t nodeCount);


    void cleanup();
    void cleanupStagingBuffers();

    Model& getModel() { return receiverModel; }
    const Model& getModel() const { return receiverModel; }
    
    VkBuffer getSurfaceBuffer() const { return surfaceBuffer; }
    VkDeviceSize getSurfaceBufferOffset() const { return surfaceBufferOffset; }
    VkBufferView getSurfaceBufferView() const { return surfaceBufferView; }
    
    VkBuffer getSurfaceVertexBuffer() const { return surfaceVertexBuffer; }
    VkDeviceSize getSurfaceVertexBufferOffset() const { return surfaceVertexBufferOffset; }
    
    VkBuffer getTriangleIndicesBuffer() const { return triangleIndicesBuffer; }
    VkDeviceSize getTriangleIndicesBufferOffset() const { return triangleIndicesBufferOffset; }
    VkBuffer getVoronoiMappingBuffer() const { return voronoiMappingBuffer; }
    VkDeviceSize getVoronoiMappingBufferOffset() const { return voronoiMappingBufferOffset; }

    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
    size_t getIntrinsicTriangleCount() const { return intrinsicTriangleCount; }
    const std::vector<glm::vec3>& getIntrinsicSurfacePositions() const { return intrinsicSurfacePositions; }
    
    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }
    
    VkBuffer getVoronoiCandidateBuffer() const { return voronoiCandidateBuffer; }
    VkDeviceSize getVoronoiCandidateBufferOffset() const { return voronoiCandidateBufferOffset; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model& receiverModel;
    Remesher& remesher;
    CommandPool& renderCommandPool;

    const float AMBIENT_TEMPERATURE = 1.0f;
    
    VkBuffer surfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceBufferOffset = 0;
    VkBufferView surfaceBufferView = VK_NULL_HANDLE;
    
    VkBuffer surfaceVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceVertexBufferOffset = 0;
    
    VkBuffer triangleIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleIndicesBufferOffset = 0;
    
    size_t intrinsicVertexCount = 0;
    size_t intrinsicTriangleCount = 0;
    std::vector<glm::vec3> intrinsicSurfacePositions;
    
    VkDescriptorSet surfaceComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceComputeSetB = VK_NULL_HANDLE;
    
    VkBuffer initStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize initStagingOffset = 0;
    VkDeviceSize initBufferSize = 0;
    
    VkBuffer voronoiMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingBufferOffset = 0;
    
    VkBuffer voronoiMappingStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingStagingOffset = 0;
    VkDeviceSize voronoiMappingBufferSize = 0;

    VkBuffer voronoiCandidateBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiCandidateBufferOffset = 0;

};
