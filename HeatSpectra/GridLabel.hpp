#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class MemoryAllocator;

class GridLabel {
public:
    struct LabelInstance {
        glm::vec3 position;       // World position
        glm::vec4 charUV;         // UV rect in atlas (x, y, width, height)
        float scale;              // Size of character
        glm::vec3 rightVec;       // Orientation vector for the quad's "right"
        glm::vec3 upVec;          // Orientation vector for the quad's "up"
    };

    struct QuadVertex {
        glm::vec3 position;
        glm::vec2 texCoord;
    };

    struct CharInfo {
        float u, v;           // Top left UV coordinate (normalized to 0-1)
        float width, height;  // Pixel dimensions (raw pixel values from atlas)
        float xadvance;       // How much to advance cursor after drawing this character
        float xoffset;        // Horizontal offset for the character
        float yoffset;        // Vertical offset for the character
    };

    GridLabel(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, VkRenderPass renderPass, CommandPool& commandPool);
    ~GridLabel();

    void updateLabels(const glm::vec3& gridSize);
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void cleanup(VulkanDevice& vulkanDevice);

private:
    void createQuadVertexBuffer(VulkanDevice& vulkanDevice);
    void createInstanceBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createFontAtlas(VulkanDevice& vulkanDevice);
    void createDescriptorPool(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createDescriptorSetLayout(VulkanDevice& vulkanDevice);
    void createDescriptorSets(VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void createPipeline(VulkanDevice& vulkanDevice, VkRenderPass renderPass);
    void generateMipmaps(VkImage image, VkFormat format, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
    
    void generateLabelInstances(const glm::vec3& gridSize);
    void addEdgeLabels(const glm::vec3& basePos, int varyingAxis, float start, float end, float interval, float scale, const glm::vec3& textRight, const glm::vec3& textUp, bool includeOrigin = true, bool isBillboard = false);
    void addTextInstances(const std::string& text, const glm::vec3& position, float scale, float charSpacing, const glm::vec3& textRight, const glm::vec3& textUp, bool isBillboard = false);
    void initializeCharMap();
    glm::vec4 getCharUV(char c);
    std::string floatToString(float value, int precision = 1);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
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
    
    std::vector<LabelInstance> labelInstances;
    uint32_t instanceCount = 0;
    glm::vec3 cachedGridSize = glm::vec3(0.0f);
    
    std::vector<CharInfo> charMap; 
};
