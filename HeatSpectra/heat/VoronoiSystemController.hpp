#pragma once

#include "HeatSystemPresets.hpp"
#include "domain/VoronoiParams.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "voronoi/VoronoiGeometryRuntime.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.h>

class MemoryAllocator;
class ModelRegistry;
class UniformBufferManager;
class VulkanDevice;
class CommandPool;
class VoronoiSystem;
struct VoronoiProduct;

class VoronoiSystemController {
public:
    struct Config {
        bool active = false;
        VoronoiParams params{};
        std::vector<uint32_t> receiverNodeModelIds;
        std::vector<std::vector<glm::vec3>> receiverGeometryPositions;
        std::vector<std::vector<uint32_t>> receiverGeometryTriangleIndices;
        std::vector<SupportingHalfedge::IntrinsicMesh> receiverIntrinsicMeshes;
        std::vector<std::vector<VoronoiGeometryRuntime::SurfaceVertex>> receiverSurfaceVertices;
        std::vector<std::vector<uint32_t>> receiverIntrinsicTriangleIndices;
        std::vector<uint32_t> receiverRuntimeModelIds;
        std::vector<VkBuffer> meshVertexBuffers;
        std::vector<VkDeviceSize> meshVertexBufferOffsets;
        std::vector<VkBuffer> meshIndexBuffers;
        std::vector<VkDeviceSize> meshIndexBufferOffsets;
        std::vector<uint32_t> meshIndexCounts;
        std::vector<glm::mat4> meshModelMatrices;
        std::vector<VkBufferView> supportingHalfedgeViews;
        std::vector<VkBufferView> supportingAngleViews;
        std::vector<VkBufferView> halfedgeViews;
        std::vector<VkBufferView> edgeViews;
        std::vector<VkBufferView> triangleViews;
        std::vector<VkBufferView> lengthViews;
        std::vector<VkBufferView> inputHalfedgeViews;
        std::vector<VkBufferView> inputEdgeViews;
        std::vector<VkBufferView> inputTriangleViews;
        std::vector<VkBufferView> inputLengthViews;
    };

    VoronoiSystemController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ModelRegistry& resourceManager,
        UniformBufferManager& uniformBufferManager,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    void createVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);
    void updateRenderContext(VkExtent2D extent, VkRenderPass renderPass);
    void updateRenderResources();
    void configure(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    VoronoiSystem* getVoronoiSystem(uint64_t socketKey) const;
    bool exportProduct(uint64_t socketKey, VoronoiProduct& outProduct) const;
    std::vector<VoronoiSystem*> getActiveSystems() const;

private:
    std::unique_ptr<VoronoiSystem> buildVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    std::unordered_map<uint64_t, std::unique_ptr<VoronoiSystem>> voronoiSystems;
    std::unordered_map<uint64_t, Config> configuredConfigs;
    VkExtent2D currentExtent = {0, 0};
    VkRenderPass currentRenderPass = VK_NULL_HANDLE;
    uint32_t maxFramesInFlight = 0;
};

