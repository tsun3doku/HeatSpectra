#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

class VulkanDevice;
class UniformBufferManager;
class MemoryAllocator;
class CommandPool;

class HeatSurfaceRenderer {
public:
    struct BufferPushConstant {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        alignas(16) glm::vec4 sourceParams{0.0f};
        uint32_t palette = 0;
        float minTemperature = 0.0f;
        float maxTemperature = 100.0f;
    };

    struct SurfaceRenderBinding {
        uint32_t runtimeModelId = 0;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexBufferOffset = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexBufferOffset = 0;
        uint32_t indexCount = 0;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        std::array<VkBufferView, 11> bufferViews{};
        VkBuffer surfaceBuffer = VK_NULL_HANDLE;
        VkDeviceSize surfaceBufferOffset = 0;
    };

    HeatSurfaceRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager, CommandPool& commandPool);
    ~HeatSurfaceRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void cleanup();
    void setPalette(uint32_t updatedPalette) { palette = updatedPalette < 4 ? updatedPalette : 0u; }
    void setRange(float minimum, float maximum) { minTemperature = minimum; maxTemperature = maximum; }
    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<SurfaceRenderBinding>& surfaces);

private:
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);
    bool createPaletteTextures();
    void destroyPaletteTextures();
    void drawModel(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, const SurfaceRenderBinding& binding) const;
    
    VkDescriptorSet allocateDescriptorSet(VkDescriptorPool pool);
    void updateDescriptorSet(VkDescriptorSet descriptorSet, uint32_t frameIndex, const SurfaceRenderBinding& binding);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
    CommandPool& commandPool;

    std::vector<VkDescriptorPool> descriptorPools;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    bool initialized = false;
    VkImage infernoImage = VK_NULL_HANDLE;
    VkDeviceMemory infernoMemory = VK_NULL_HANDLE;
    VkImageView infernoView = VK_NULL_HANDLE;
    VkImage viridisImage = VK_NULL_HANDLE;
    VkDeviceMemory viridisMemory = VK_NULL_HANDLE;
    VkImageView viridisView = VK_NULL_HANDLE;
    VkImage inferno2Image = VK_NULL_HANDLE;
    VkDeviceMemory inferno2Memory = VK_NULL_HANDLE;
    VkImageView inferno2View = VK_NULL_HANDLE;
    VkImage parulaImage = VK_NULL_HANDLE;
    VkDeviceMemory parulaMemory = VK_NULL_HANDLE;
    VkImageView parulaView = VK_NULL_HANDLE;
    VkSampler paletteSampler = VK_NULL_HANDLE;
    uint32_t palette = 0;
    float minTemperature = 0.0f;
    float maxTemperature = 100.0f;
};

static_assert(offsetof(HeatSurfaceRenderer::BufferPushConstant, palette) == 80);
static_assert(offsetof(HeatSurfaceRenderer::BufferPushConstant, minTemperature) == 84);
static_assert(offsetof(HeatSurfaceRenderer::BufferPushConstant, maxTemperature) == 88);
