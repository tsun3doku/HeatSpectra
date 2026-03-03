#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "util/GlyphText.hpp"

#include <cstdint>
#include <string>
#include <vector>

class VulkanDevice;
class CommandPool;

class TimingRenderer {
public:
    TimingRenderer(
        VulkanDevice& vulkanDevice,
        uint32_t maxFramesInFlight,
        VkRenderPass renderPass,
        uint32_t subpassIndex,
        CommandPool& commandPool);
    ~TimingRenderer();

    void setLines(const std::vector<std::string>& lines);
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkExtent2D extent);
    void cleanup();

private:
    struct QuadVertex {
        glm::vec2 position;
        glm::vec2 texCoord;
    };

    struct GlyphInstance {
        glm::vec2 centerPx;
        glm::vec2 sizePx;
        glm::vec4 charUV;
        glm::vec4 color;
    };

    void createQuadVertexBuffer();
    void createInstanceBuffers(uint32_t maxFramesInFlight);
    void createFontAtlas();
    void createDescriptorPool(uint32_t maxFramesInFlight);
    void createDescriptorSetLayout();
    void createDescriptorSets(uint32_t maxFramesInFlight);
    void createPipeline(VkRenderPass renderPass, uint32_t subpassIndex);
    void buildGlyphInstances();

    VulkanDevice& vulkanDevice;
    CommandPool& commandPool;

    VkBuffer quadVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory quadVertexBufferMemory = VK_NULL_HANDLE;

    std::vector<VkBuffer> instanceBuffers;
    std::vector<VkDeviceMemory> instanceBufferMemories;
    std::vector<void*> instanceBuffersMapped;

    VkImage fontAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory fontAtlasMemory = VK_NULL_HANDLE;
    VkImageView fontAtlasView = VK_NULL_HANDLE;
    VkSampler fontSampler = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    GlyphText glyphText;
    std::vector<std::string> activeLines;
    std::vector<GlyphInstance> glyphInstances;
    uint32_t glyphCount = 0;
    uint32_t maxGlyphCapacity = 512;
};
