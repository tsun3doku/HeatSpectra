#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class MemoryAllocator;

class VoronoiRenderer {
public:
    struct RenderBinding {
        uint64_t bindingKey = 0;
        uint32_t runtimeModelId = 0;
        uint32_t vertexCount = 0;
        VkBuffer seedBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedOffset = 0;
        VkBuffer neighborBuffer = VK_NULL_HANDLE;
        VkDeviceSize neighborOffset = 0;
        VkBufferView supportingHalfedgeView = VK_NULL_HANDLE;
        VkBufferView supportingAngleView = VK_NULL_HANDLE;
        VkBufferView halfedgeView = VK_NULL_HANDLE;
        VkBufferView edgeView = VK_NULL_HANDLE;
        VkBufferView triangleView = VK_NULL_HANDLE;
        VkBufferView lengthView = VK_NULL_HANDLE;
        VkBufferView inputHalfedgeView = VK_NULL_HANDLE;
        VkBufferView inputEdgeView = VK_NULL_HANDLE;
        VkBufferView inputTriangleView = VK_NULL_HANDLE;
        VkBufferView inputLengthView = VK_NULL_HANDLE;
        VkBuffer candidateBuffer = VK_NULL_HANDLE;
        VkDeviceSize candidateOffset = 0;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexOffset = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexOffset = 0;
        uint32_t indexCount = 0;
        glm::mat4 modelMatrix{1.0f};
        float canonicalToWorldScale = 1.0f;
    };

    VoronoiRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager, CommandPool& commandPool);
    ~VoronoiRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const std::vector<RenderBinding>& bindings);
    void cleanup();

private:
    struct SurfacePushConstant {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        float alpha = 1.0f;
        float canonicalToWorldScale = 1.0f;
        float padding0 = 0.0f;
        float padding1 = 0.0f;
    };

    uint32_t calculateMipLevels(uint32_t width, uint32_t height);
    bool createWireframeTexture();

    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);
    
    VkDescriptorSet allocateDescriptorSet(VkDescriptorPool pool);
    void updateDescriptorSet(VkDescriptorSet set, uint32_t frameIndex, const RenderBinding& binding);
    void drawBinding(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, const RenderBinding& binding) const;

    VulkanDevice& vulkanDevice;
    MemoryAllocator& allocator;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptorPools;

    VkImage wireframeTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory wireframeTextureMemory = VK_NULL_HANDLE;
    VkImageView wireframeTextureView = VK_NULL_HANDLE;
    VkSampler wireframeTextureSampler = VK_NULL_HANDLE;
    
    bool initialized = false;
};
