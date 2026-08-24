#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "heat/HeatGpuStructs.hpp"

#include <vector>

class MemoryAllocator;
class UniformBufferManager;
class VulkanDevice;

class HeatContactLevelSetRenderer {
public:
    struct RenderBinding {
        std::vector<VkImageView> sdfImageViews;
        std::vector<VkImageView> psiImageViews;
        VkSampler sdfSampler = VK_NULL_HANDLE;
        VkBuffer regionBuffer = VK_NULL_HANDLE;
        VkDeviceSize regionBufferOffset = 0;
        VkDeviceSize regionBufferSize = 0;
        VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
        VkDeviceSize indirectDrawOffset = 0;
        glm::vec3 gridMin{0.0f};
        glm::ivec3 gridDim{0};
        float cellSize = 0.0f;
        float range = 1.0f;
    };

    HeatContactLevelSetRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager);
    ~HeatContactLevelSetRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<RenderBinding>& bindings);
    void cleanup();

    static constexpr uint32_t MaxSdfTextures = 32;
    static constexpr uint32_t MaxPsiTextures = 32;
    static constexpr uint32_t MaxBindingsPerFrame = 8;

private:
    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);
    bool createUniformBuffers(uint32_t maxFramesInFlight);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    std::vector<std::vector<VkDescriptorSet>> descriptorSets;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::vector<VkBuffer> uboBuffers;
    std::vector<VkDeviceSize> uboBufferOffsets;
    std::vector<void*> uboMappedPtrs;

    bool initialized = false;
};