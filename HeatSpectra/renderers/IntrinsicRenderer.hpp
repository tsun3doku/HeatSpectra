#pragma once

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class Model;
class iODT;
class Remesher;

class IntrinsicRenderer {
public:
    IntrinsicRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager, CommandPool& commandPool,
        VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    ~IntrinsicRenderer();
    void cleanup();

    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);

    void renderSupportingHalfedges(VkCommandBuffer commandBuffer, uint32_t currentFrame, Remesher& remesher);
    void renderIntrinsicNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, Remesher& remesher, float normalLength);
    void renderIntrinsicVertexNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, Remesher& remesher, float normalLength);

private:
    bool initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    uint32_t calculateMipLevels(uint32_t width, uint32_t height);
    bool createWireframeTexture();

    bool createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight);
    bool createSupportingHalfedgeDescriptorSetLayout();

    bool createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight);
    bool createIntrinsicNormalsDescriptorSetLayout();
    bool createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight);
    bool createIntrinsicVertexNormalsDescriptorSetLayout();

    bool createSupportingHalfedgePipeline(VkRenderPass renderPass, uint32_t subpassIndex);
    bool createIntrinsicNormalsPipeline(VkRenderPass renderPass, uint32_t subpassIndex);
    bool createIntrinsicVertexNormalsPipeline(VkRenderPass renderPass, uint32_t subpassIndex);

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;

    VkDescriptorPool supportingHalfedgeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout supportingHalfedgeDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelSupportingHalfedgeDescriptorSets;

    VkImage wireframeTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory wireframeTextureMemory = VK_NULL_HANDLE;
    VkImageView wireframeTextureView = VK_NULL_HANDLE;
    VkSampler wireframeTextureSampler = VK_NULL_HANDLE;

    VkDescriptorPool intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelIntrinsicNormalsDescriptorSets;

    VkDescriptorPool intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelIntrinsicVertexNormalsDescriptorSets;

    VkPipeline supportingHalfedgePipeline = VK_NULL_HANDLE;
    VkPipelineLayout supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;

    bool initialized = false;
};
