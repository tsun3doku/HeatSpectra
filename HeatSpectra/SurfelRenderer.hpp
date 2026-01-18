#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include "Structs.hpp"

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;

class SurfelRenderer {
public:
    struct Surfel {
        alignas(16) glm::mat4 modelMatrix;  
        float surfelRadius;
    };

    SurfelRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager);
    ~SurfelRenderer();

    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    
    void createCircleGeometry(int segments = 16);  
    void render(VkCommandBuffer cmdBuffer, VkBuffer surfaceBuffer, VkDeviceSize surfaceBufferOffset, uint32_t surfelCount, 
                const Surfel& surfel, uint32_t frameIndex);

    void cleanup();

    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }
    uint32_t getIndexCount() const { return indexCount; }
    const SurfelParams& getParams() const { return params; }
    VkBuffer getSurfelParamsBuffer() const { return surfelParamsBuffer; }
    VkDeviceSize getSurfelParamsBufferOffset() const { return surfelParamsBufferOffset; }

private:
    void createSurfelBuffers(uint32_t maxFramesInFlight);
    void createSurfelParamsBuffer();
    void createSurfelDescriptorSetLayout();
    void createSurfelDescriptorPool(uint32_t maxFramesInFlight);
    void createSurfelDescriptorSets(uint32_t maxFramesInFlight);
    void createSurfelPipeline(VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;

    // Circle geometry
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexBufferOffset = 0;
    VkDeviceSize indexBufferOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceSize> uniformBufferOffsets;
    std::vector<void*> mappedUniforms;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    bool initialized = false;
    
    SurfelParams params;
    VkBuffer surfelParamsBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfelParamsBufferOffset = 0;
    void* mappedSurfelParamsData = nullptr;
};
