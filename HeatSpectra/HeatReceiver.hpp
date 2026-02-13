#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include "Structs.hpp"

class VulkanDevice;
class MemoryAllocator;
class Model;
class CommandPool;
class ResourceManager;
class HeatSource;

class HeatReceiver {
public:
    HeatReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,Model& receiverModel,
                 ResourceManager& resourceManager, CommandPool& renderCommandPool);
    ~HeatReceiver();

    void createReceiverBuffers();

    void initializeReceiverBuffer();

    void stageVoronoiSurfaceMapping(const std::vector<uint32_t>& cellIndices);
    void stageVoronoiTriangleMapping(const std::vector<uint32_t>& cellIndices);

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

	void uploadContactPairs(const std::vector<ContactPairGPU>& pairs);

    void updateContactDescriptors(
        VkDescriptorSetLayout contactLayout,
        VkDescriptorPool contactPool,
        HeatSource& heatSource,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer injectionKBuffer,
        VkDeviceSize injectionKBufferOffset,
        VkBuffer injectionKTBuffer,
        VkDeviceSize injectionKTBufferOffset,
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

    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
    size_t getIntrinsicTriangleCount() const { return intrinsicTriangleCount; }
    
    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }

    VkDescriptorSet getContactComputeSetA() const { return contactComputeSetA; }
    VkDescriptorSet getContactComputeSetB() const { return contactComputeSetB; }
    
    VkBuffer getVoronoiCandidateBuffer() const { return voronoiCandidateBuffer; }
    VkDeviceSize getVoronoiCandidateBufferOffset() const { return voronoiCandidateBufferOffset; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model& receiverModel;
    ResourceManager& resourceManager;
    CommandPool& renderCommandPool;

    const float AMBIENT_TEMPERATURE = 1.0f;
    
    VkBuffer surfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceBufferOffset = 0;
    VkBufferView surfaceBufferView = VK_NULL_HANDLE;
    
    VkBuffer surfaceVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceVertexBufferOffset = 0;
    
    VkBuffer triangleIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleIndicesBufferOffset = 0;

    VkBuffer triangleCentroidBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleCentroidBufferOffset = 0;
    
    size_t intrinsicVertexCount = 0;
    size_t intrinsicTriangleCount = 0;
    
    VkDescriptorSet surfaceComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceComputeSetB = VK_NULL_HANDLE;

    VkDescriptorSet contactComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet contactComputeSetB = VK_NULL_HANDLE;
    
    VkBuffer initStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize initStagingOffset = 0;
    VkDeviceSize initBufferSize = 0;
    
    VkBuffer voronoiMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingBufferOffset = 0;
    
    VkBuffer voronoiMappingStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingStagingOffset = 0;
    VkDeviceSize voronoiMappingBufferSize = 0;

    VkBuffer voronoiTriangleMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiTriangleMappingBufferOffset = 0;
    VkBuffer voronoiTriangleMappingStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiTriangleMappingStagingOffset = 0;
    VkDeviceSize voronoiTriangleMappingBufferSize = 0;

    VkBuffer voronoiCandidateBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiCandidateBufferOffset = 0;

	VkBuffer contactPairBuffer = VK_NULL_HANDLE;
	VkDeviceSize contactPairBufferOffset = 0;

};
