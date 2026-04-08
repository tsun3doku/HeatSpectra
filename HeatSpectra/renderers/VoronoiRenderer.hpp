#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

class VulkanDevice;
class UniformBufferManager;
class CommandPool;

class VoronoiRenderer {
public:
    VoronoiRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager, CommandPool& commandPool);
    ~VoronoiRenderer();

    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    
    void updateDescriptors(uint32_t frameIndex,
        uint32_t vertexCount, VkBuffer seedBuffer, VkDeviceSize seedOffset,
        VkBuffer neighborBuffer, VkDeviceSize neighborOffset,
        VkBufferView supportingHalfedgeView, VkBufferView supportingAngleView,
        VkBufferView halfedgeView, VkBufferView edgeView,
        VkBufferView triangleView, VkBufferView lengthView,
        VkBufferView inputHalfedgeView, VkBufferView inputEdgeView,
        VkBufferView inputTriangleView, VkBufferView inputLengthView,
        VkBuffer candidateBuffer, VkDeviceSize candidateOffset);

    void render(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
               VkBuffer indexBuffer, VkDeviceSize indexOffset, uint32_t indexCount, 
               uint32_t frameIndex, const glm::mat4& modelMatrix);

    void cleanup();

private:
    uint32_t calculateMipLevels(uint32_t width, uint32_t height);
    bool createWireframeTexture();

    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSets(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass);
    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    VkImage wireframeTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory wireframeTextureMemory = VK_NULL_HANDLE;
    VkImageView wireframeTextureView = VK_NULL_HANDLE;
    VkSampler wireframeTextureSampler = VK_NULL_HANDLE;
    
    bool initialized = false;
    
    uint32_t currentVertexCount = 0;
    VkBuffer currentCandidateBuffer = VK_NULL_HANDLE;
};
