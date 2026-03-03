#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Model;
class ResourceManager;
class UniformBufferManager;
class iODT;
class VulkanDevice;
class CommandPool;
class TimingRenderer;
class GridRenderer;
class GizmoRenderer;
class VkFrameGraphRuntime;

namespace render {
class GeometryPass;
}

namespace render {
class OverlayPass : public Pass {
public:
    OverlayPass(
        VulkanDevice& device,
        VkFrameGraphRuntime& frameGraphRuntime,
        ResourceManager& resources,
        UniformBufferManager& ubo,
        GeometryPass& geometry,
        uint32_t framesInFlight,
        CommandPool& pool,
        framegraph::PassId passId,
        framegraph::ResourceId depthResolveId,
        framegraph::ResourceId depthMsaaId);
    ~OverlayPass() override;

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(const FrameContext& context, const SceneView& sceneView, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) override;
    void destroy() override;

    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void setTimingOverlayLines(const std::vector<std::string>& lines);
    void updateGridLabels(const glm::vec3& gridSize);

private:
    bool createDepthSampler();
    bool createOutlineDescriptorPool(uint32_t maxFramesInFlight);
    bool createOutlineDescriptorSetLayout();
    bool createOutlineDescriptorSets(uint32_t maxFramesInFlight);
    bool createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight);
    bool createSupportingHalfedgeDescriptorSetLayout();
    bool createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight);
    bool createIntrinsicNormalsDescriptorSetLayout();
    bool createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight);
    bool createIntrinsicVertexNormalsDescriptorSetLayout();

    bool createOutlinePipeline();
    bool createSupportingHalfedgePipeline();
    bool createIntrinsicNormalsPipeline();
    bool createIntrinsicVertexNormalsPipeline();

    GeometryPass& geometryPass;
    ::VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    uint32_t maxFramesInFlight = 0;
    framegraph::PassId passId{};
    framegraph::ResourceId depthResolveId{};
    framegraph::ResourceId depthMsaaId{};

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
    std::unique_ptr<TimingRenderer> timingOverlay;
    std::unique_ptr<GridRenderer> gridRenderer;
    std::unique_ptr<GizmoRenderer> gizmoRenderer;
    bool ready = false;
};

} // namespace render

