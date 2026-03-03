#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;

class ContactLineRenderer {
public:
    struct LineVertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    ContactLineRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager);
    ~ContactLineRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);

    void uploadOutlines(const std::vector<LineVertex>& vertices);
    void uploadCorrespondences(const std::vector<LineVertex>& vertices);
    void render(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& modelMatrix, VkExtent2D extent);

    void setVisible(bool vis) { visible = vis; }
    bool isVisible() const { return visible; }

    void cleanup();

private:
    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSets(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);
    bool createOutlinePipeline(VkRenderPass renderPass, uint32_t subpass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline outlinePipeline = VK_NULL_HANDLE;

    VkBuffer outlineBuffer = VK_NULL_HANDLE;
    VkDeviceSize outlineBufferOffset = 0;
    uint32_t outlineVertexCount = 0;

    VkBuffer correspondenceBuffer = VK_NULL_HANDLE;
    VkDeviceSize correspondenceBufferOffset = 0;
    uint32_t correspondenceVertexCount = 0;

    bool visible = true;
    bool initialized = false;
};

