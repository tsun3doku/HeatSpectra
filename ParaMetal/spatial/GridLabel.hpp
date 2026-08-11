#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include "util/GlyphText.hpp"
#include "util/Units.hpp"

class MemoryAllocator;
class VulkanDevice;
class UniformBufferManager;
class CommandPool;

class GridLabel {
public:
    struct LabelInstance {
        glm::vec3 position;       // World position
        glm::vec4 charUV;         // UV rect in atlas (x, y, width, height)
        float scale;              // Size of character
        glm::vec3 rightVec;       // Orientation vector for the quad's "right"
        glm::vec3 upVec;          // Orientation vector for the quad's "up"
        glm::vec4 color;           // Axis label color
    };

    struct QuadVertex {
        glm::vec3 position;
        glm::vec2 texCoord;
    };

    GridLabel(
        VulkanDevice& vulkanDevice, 
        MemoryAllocator& allocator, 
        UniformBufferManager& uniformBufferManager, 
        uint32_t maxFramesInFlight, 
        VkRenderPass renderPass, 
        uint32_t subpass, 
        CommandPool& commandPool
    );
    ~GridLabel();

    void updateLabels(const glm::vec3& gridSize);
    void setWorldUnit(units::LengthUnit unit);
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void cleanup(VulkanDevice& vulkanDevice);

private:
    void createQuadVertexBuffer(VulkanDevice& vulkanDevice);
    void createInstanceBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createFontAtlas(VulkanDevice& vulkanDevice);
    void createDescriptorPool(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createDescriptorSetLayout(VulkanDevice& vulkanDevice);
    void createDescriptorSets(VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void createPipeline(VulkanDevice& vulkanDevice, VkRenderPass renderPass, uint32_t subpass);
    void generateLabelInstances(const glm::vec3& gridSize);
    void addEdgeLabels(
        const glm::vec3& basePos, 
        int varyingAxis, 
        float halfExtent,
        float interval, 
        float scale, 
        const glm::vec3& textRight, 
        const glm::vec3& textUp
    );
    void addTextInstances(
        const std::string& text, 
        const glm::vec3& position, 
        float scale, 
        const glm::vec4& color,
        const glm::vec3& textRight, 
        const glm::vec3& textUp
    );
    std::string floatToString(float value, int precision = 1);
    static float calculateInterval(const glm::vec3& gridSize);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& allocator;
    UniformBufferManager& uniformBufferManager;
    CommandPool& commandPool;

    VkBuffer quadVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize quadVertexBufferOffset = 0;

    std::vector<VkBuffer> instanceBuffers;
    std::vector<VkDeviceSize> instanceBufferOffsets;
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
    
    std::vector<LabelInstance> labelInstances;
    uint32_t instanceCount = 0;
    glm::vec3 cachedGridSize = glm::vec3(0.0f);
    units::LengthUnit worldUnit = units::defaultLengthUnit();
    bool labelsDirty = true;

    GlyphText glyphText;
};
