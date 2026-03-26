#pragma once

#include "domain/RemeshData.hpp"

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;
class CommandPool;
class Model;
class iODT;
class ResourceManager;
class RuntimeIntrinsicCache;

class IntrinsicRenderer {
public:
    IntrinsicRenderer(VulkanDevice& device, MemoryAllocator& allocator, RuntimeIntrinsicCache& remeshResources, UniformBufferManager& uniformBufferManager, CommandPool& commandPool,
        VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    ~IntrinsicRenderer();
    void cleanup();

    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);

    void updatePayloadForModel(Model* model, const IntrinsicMeshData& intrinsic, uint32_t maxFramesInFlight);
    void renderSupportingHalfedges(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ResourceManager& resourceManager);
    void renderIntrinsicNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ResourceManager& resourceManager, float normalLength);
    void renderIntrinsicVertexNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ResourceManager& resourceManager, float normalLength);

private:
    struct PayloadState {
        size_t triangleCount = 0;
        size_t vertexCount = 0;
        float averageTriangleArea = 0.0f;
        bool uploaded = false;
        VkBuffer bufferS = VK_NULL_HANDLE;
        VkBuffer bufferA = VK_NULL_HANDLE;
        VkBuffer bufferH = VK_NULL_HANDLE;
        VkBuffer bufferE = VK_NULL_HANDLE;
        VkBuffer bufferT = VK_NULL_HANDLE;
        VkBuffer bufferL = VK_NULL_HANDLE;
        VkBuffer bufferHInput = VK_NULL_HANDLE;
        VkBuffer bufferEInput = VK_NULL_HANDLE;
        VkBuffer bufferTInput = VK_NULL_HANDLE;
        VkBuffer bufferLInput = VK_NULL_HANDLE;
        VkBuffer intrinsicTriangleBuffer = VK_NULL_HANDLE;
        VkBuffer intrinsicVertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize offsetS = 0;
        VkDeviceSize offsetA = 0;
        VkDeviceSize offsetH = 0;
        VkDeviceSize offsetE = 0;
        VkDeviceSize offsetT = 0;
        VkDeviceSize offsetL = 0;
        VkDeviceSize offsetHInput = 0;
        VkDeviceSize offsetEInput = 0;
        VkDeviceSize offsetTInput = 0;
        VkDeviceSize offsetLInput = 0;
        VkDeviceSize triangleGeometryOffset = 0;
        VkDeviceSize vertexGeometryOffset = 0;
        VkBufferView viewS = VK_NULL_HANDLE;
        VkBufferView viewA = VK_NULL_HANDLE;
        VkBufferView viewH = VK_NULL_HANDLE;
        VkBufferView viewE = VK_NULL_HANDLE;
        VkBufferView viewT = VK_NULL_HANDLE;
        VkBufferView viewL = VK_NULL_HANDLE;
        VkBufferView viewHInput = VK_NULL_HANDLE;
        VkBufferView viewEInput = VK_NULL_HANDLE;
        VkBufferView viewTInput = VK_NULL_HANDLE;
        VkBufferView viewLInput = VK_NULL_HANDLE;
    };

    bool initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    uint32_t calculateMipLevels(uint32_t width, uint32_t height);
    bool createWireframeTexture();
    void pruneStaleModelResources(const ResourceManager& resourceManager);
    void releaseDescriptorSetsForModel(Model* model);
    bool uploadPayloadState(Model* model, const IntrinsicMeshData& intrinsic, PayloadState& state);
    void updatePayloadDescriptorSetsForModel(Model* model, const PayloadState& state, uint32_t maxFramesInFlight);
    void updatePayloadNormalsDescriptorSetsForModel(Model* model, const PayloadState& state, uint32_t maxFramesInFlight);
    void updatePayloadVertexNormalsDescriptorSetsForModel(Model* model, const PayloadState& state, uint32_t maxFramesInFlight);
    void releasePayloadState(PayloadState& state);

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
    MemoryAllocator& allocator;
    RuntimeIntrinsicCache& remeshResources;
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
    std::unordered_map<Model*, PayloadState> payloadStateByModel;

    VkPipeline supportingHalfedgePipeline = VK_NULL_HANDLE;
    VkPipelineLayout supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;

    bool initialized = false;
};
