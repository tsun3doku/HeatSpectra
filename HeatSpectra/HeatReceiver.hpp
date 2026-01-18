#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include "Structs.hpp"
#include "SupportingHalfedge.hpp" 

class VulkanDevice;
class MemoryAllocator;
class Model;
class CommandPool;
class ResourceManager;
class SupportingHalfedge;
class UniformBufferManager;
class HeatSource;
struct FEAMesh;

class HeatReceiver {
public:
    HeatReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,Model& receiverModel,
                 ResourceManager& resourceManager, CommandPool& renderCommandPool, uint32_t maxFramesInFlight);
    ~HeatReceiver();

    void createReceiverBuffers();

    void initializeReceiverBuffer();

    void computeVoronoiSurfaceMapping(class VoronoiSeeder* seeder);
    void updateDescriptors(
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorSetLayout renderLayout,
        VkDescriptorPool surfacePool,
        VkDescriptorPool renderPool,
        UniformBufferManager& uboManager,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t maxFramesInFlight,
        uint32_t nodeCount);

    void recreateContactDescriptors(
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
        VkDescriptorSetLayout renderLayout,
        VkDescriptorPool renderPool,
        UniformBufferManager& uboManager,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t maxFramesInFlight,
        uint32_t nodeCount);


    void cleanup();
    void cleanupStagingBuffers();

    // Getters
    Model& getModel() { return receiverModel; }
    const Model& getModel() const { return receiverModel; }
    
    VkBuffer getSurfaceBuffer() const { return surfaceBuffer; }
    VkDeviceSize getSurfaceBufferOffset() const { return surfaceBufferOffset; }
    VkBufferView getSurfaceBufferView() const { return surfaceBufferView; }
    
    VkBuffer getSurfaceVertexBuffer() const { return surfaceVertexBuffer; }
    VkDeviceSize getSurfaceVertexBufferOffset() const { return surfaceVertexBufferOffset; }
    
    VkBuffer getTriangleIndicesBuffer() const { return triangleIndicesBuffer; }
    VkDeviceSize getTriangleIndicesBufferOffset() const { return triangleIndicesBufferOffset; }

    VkBuffer getTriangleCentroidBuffer() const { return triangleCentroidBuffer; }
    VkDeviceSize getTriangleCentroidBufferOffset() const { return triangleCentroidBufferOffset; }
    
    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
    size_t getIntrinsicTriangleCount() const { return intrinsicTriangleCount; }
    
    const std::vector<VkDescriptorSet>& getHeatRenderDescriptorSets() const { return heatRenderDescriptorSets; }
    
    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }

    VkDescriptorSet getContactComputeSetA() const { return contactComputeSetA; }
    VkDescriptorSet getContactComputeSetB() const { return contactComputeSetB; }
    
    VkBuffer getVoronoiMappingBuffer() const { return voronoiMappingBuffer; }
    VkDeviceSize getVoronoiMappingBufferOffset() const { return voronoiMappingBufferOffset; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model& receiverModel;
    ResourceManager& resourceManager;
    CommandPool& renderCommandPool;
    uint32_t maxFramesInFlight;

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
    
    std::vector<VkDescriptorSet> heatRenderDescriptorSets;
    VkDescriptorSet surfaceComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceComputeSetB = VK_NULL_HANDLE;

    VkDescriptorSet contactComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet contactComputeSetB = VK_NULL_HANDLE;
    
    VkBuffer initStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize initStagingOffset = 0;
    void* initStagingData = nullptr;
    VkDeviceSize initBufferSize = 0;
    
    VkBuffer interpStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize interpStagingOffset = 0;
    void* interpStagingData = nullptr;
    VkDeviceSize interpBufferSize = 0;
    
    VkBuffer voronoiMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingBufferOffset = 0;
    
    VkBuffer voronoiMappingStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingStagingOffset = 0;
    void* voronoiMappingStagingData = nullptr;
    VkDeviceSize voronoiMappingBufferSize = 0;

};



