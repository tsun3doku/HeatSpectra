#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

class MemoryAllocator;
class UniformBufferManager;
class VulkanDevice;
class CommandPool;

class VectorArrowRenderer {
public:
    struct VectorRenderBinding {
        uint64_t bindingKey = 0;
        VkBuffer surfaceBuffer = VK_NULL_HANDLE;
        VkDeviceSize surfaceBufferOffset = 0;
        VkBuffer gradientBuffer = VK_NULL_HANDLE;
        VkDeviceSize gradientBufferOffset = 0;
        uint32_t sampleCount = 0;
        glm::mat4 modelMatrix{1.0f};
        float scale = 1.0f;
        float canonicalToWorldScale = 1.0f;
    };

    VectorArrowRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager, CommandPool& commandPool);
    ~VectorArrowRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<VectorRenderBinding>& vectors);
    void cleanup();

private:
    struct PushConstants {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        float scale = 1.0f;
        float normalOffset = 0.001f;
        float minLength = 0.00025f;
        float baseLength = 0.005f;
    };

    bool createArrowGeometry();
    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);
    
    VkDescriptorSet allocateDescriptorSet(VkDescriptorPool pool);
    void updateDescriptorSet(VkDescriptorSet set, uint32_t frameIndex, const VectorRenderBinding& binding);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
    CommandPool& commandPool;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexBufferOffset = 0;
    uint32_t vertexCount = 0;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptorPools;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    bool initialized = false;
};
