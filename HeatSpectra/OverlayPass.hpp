#pragma once

#include "FrameGraph.hpp"

#include <unordered_map>
#include <vector>

class SceneRenderer;
class Model;
class UniformBufferManager;
class iODT;
class VulkanDevice;
class FrameGraph;

namespace render {
class GeometryPass;
}

namespace render {

class OverlayPass : public Pass {
public:
    explicit OverlayPass(SceneRenderer& sceneRenderer, GeometryPass& geometryPass);

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(
        const FrameContext& frameContext,
        const SceneView& sceneView,
        const RenderFlags& flags,
        const OverlayParams& overlayParams,
        RenderServices& services) override;
    void destroy() override;

    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

private:
    void createDepthSampler();
    void createOutlineDescriptorPool(uint32_t maxFramesInFlight);
    void createOutlineDescriptorSetLayout();
    void createOutlineDescriptorSets(uint32_t maxFramesInFlight);
    void createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight);
    void createSupportingHalfedgeDescriptorSetLayout();
    void createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight);
    void createIntrinsicNormalsDescriptorSetLayout();
    void createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight);
    void createIntrinsicVertexNormalsDescriptorSetLayout();

    void createOutlinePipeline(VkExtent2D extent);
    void createSupportingHalfedgePipeline(VkExtent2D extent);
    void createIntrinsicNormalsPipeline(VkExtent2D extent);
    void createIntrinsicVertexNormalsPipeline(VkExtent2D extent);

    SceneRenderer& sceneRenderer;
    GeometryPass& geometryPass;
    ::VulkanDevice& vulkanDevice;
    ::FrameGraph& frameGraph;

    VkDescriptorPool outlineDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout outlineDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> outlineDescriptorSets;

    VkDescriptorPool supportingHalfedgeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout supportingHalfedgeDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelSupportingHalfedgeDescriptorSets;

    VkDescriptorPool intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelIntrinsicNormalsDescriptorSets;

    VkDescriptorPool intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelIntrinsicVertexNormalsDescriptorSets;

    VkSampler depthSampler = VK_NULL_HANDLE;

    VkPipeline outlinePipeline = VK_NULL_HANDLE;
    VkPipelineLayout outlinePipelineLayout = VK_NULL_HANDLE;
    VkPipeline supportingHalfedgePipeline = VK_NULL_HANDLE;
    VkPipelineLayout supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;
};

} // namespace render

