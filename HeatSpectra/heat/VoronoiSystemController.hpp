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
class ResourceManager;
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
        ResourceManager& resourceManager,
        UniformBufferManager& uniformBufferManager,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    void createVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);
    void recreateVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);
    void configure(const Config& config);
    void disable();
    VoronoiSystem* getVoronoiSystem() const;
    bool exportProduct(VoronoiProduct& outProduct) const;

private:
    std::unique_ptr<VoronoiSystem> buildVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    std::unique_ptr<VoronoiSystem> voronoiSystem;
    Config configuredConfig{};
    uint32_t maxFramesInFlight = 0;
};
