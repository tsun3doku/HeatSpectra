#pragma once

#include "VoronoiSystemRuntime.hpp"
#include "voronoi/VoronoiBuilder.hpp"
#include "voronoi/VoronoiSurfaceRuntime.hpp"
#include "voronoi/VoronoiSystemResources.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

class VoronoiModelRuntime;
class MemoryAllocator;
class PointRenderer;
class ModelRegistry;
class UniformBufferManager;
class VulkanDevice;
class CommandPool;
class VoronoiGeoCompute;
class VoronoiCandidateCompute;
class VoronoiRenderer;
class VoronoiSurfaceStage;

class VoronoiSystem {
public:
    VoronoiSystem(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ModelRegistry& resourceManager,
        UniformBufferManager& uniformBufferManager,
        uint32_t maxFramesInFlight,
        CommandPool& renderCommandPool,
        VkExtent2D extent,
        VkRenderPass renderPass);
    ~VoronoiSystem();

    bool isInitialized() const { return initialized; }
    bool isReady() const { return runtime.isReady(); }

    void updateRenderResources(VkRenderPass renderPass);

    void setReceiverPayloads(
        const std::vector<uint32_t>& receiverNodeModelIds,
        const std::vector<std::vector<glm::vec3>>& receiverGeometryPositions,
        const std::vector<std::vector<uint32_t>>& receiverGeometryTriangleIndices,
        const std::vector<SupportingHalfedge::IntrinsicMesh>& receiverIntrinsicMeshes,
        const std::vector<std::vector<VoronoiGeometryRuntime::SurfaceVertex>>& receiverSurfaceVertices,
        const std::vector<std::vector<uint32_t>>& receiverIntrinsicTriangleIndices,
        const std::vector<uint32_t>& receiverModelIds,
        const std::vector<VkBuffer>& meshVertexBuffers,
        const std::vector<VkDeviceSize>& meshVertexBufferOffsets,
        const std::vector<VkBuffer>& meshIndexBuffers,
        const std::vector<VkDeviceSize>& meshIndexBufferOffsets,
        const std::vector<uint32_t>& meshIndexCounts,
        const std::vector<glm::mat4>& meshModelMatrices,
        const std::vector<VkBufferView>& supportingHalfedgeViews,
        const std::vector<VkBufferView>& supportingAngleViews,
        const std::vector<VkBufferView>& halfedgeViews,
        const std::vector<VkBufferView>& edgeViews,
        const std::vector<VkBufferView>& triangleViews,
        const std::vector<VkBufferView>& lengthViews,
        const std::vector<VkBufferView>& inputHalfedgeViews,
        const std::vector<VkBufferView>& inputEdgeViews,
        const std::vector<VkBufferView>& inputTriangleViews,
        const std::vector<VkBufferView>& inputLengthViews);
    void clearReceiverPayloads();
    void setParams(const VoronoiParams& params);
    bool ensureConfigured();

    void renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent);

    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& getModelRuntimes() const { return runtime.getModelRuntimes(); }
    const std::vector<VoronoiDomain>& getReceiverVoronoiDomains() const { return runtime.getReceiverVoronoiDomains(); }
    const VoronoiDomain* findReceiverDomain(uint32_t receiverModelId) const { return runtime.findReceiverDomain(receiverModelId); }
    VoronoiRenderer* voronoiRendererPtr() const { return voronoiRenderer.get(); }
    PointRenderer* pointRendererPtr() const { return pointRenderer.get(); }

    VkBuffer getSeedPositionBuffer() const { return runtime.voronoiResourcesRef().seedPositionBuffer; }
    VkDeviceSize getSeedPositionBufferOffset() const { return runtime.voronoiResourcesRef().seedPositionBufferOffset; }
    VkBuffer getVoronoiNeighborBuffer() const { return runtime.voronoiResourcesRef().neighborIndicesBuffer; }
    VkDeviceSize getVoronoiNeighborBufferOffset() const { return runtime.voronoiResourcesRef().neighborIndicesBufferOffset; }
    uint32_t getVoronoiNodeCount() const { return runtime.getVoronoiNodeCount(); }

    VoronoiSystemResources& resourcesRef() { return runtime.resourcesRef(); }
    const VoronoiSystemResources& resourcesRef() const { return runtime.resourcesRef(); }
    VoronoiResources& voronoiResourcesRef() { return runtime.voronoiResourcesRef(); }
    const VoronoiResources& voronoiResourcesRef() const { return runtime.voronoiResourcesRef(); }
    VoronoiSystemRuntime& runtimeRef() { return runtime; }
    const VoronoiSystemRuntime& runtimeRef() const { return runtime; }

    void cleanupResources();
    void cleanup();

private:
    void failInitialization(const char* stage);
    void initializeVoronoiRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeVoronoiGeoCompute();
    void initializeVoronoiCandidateCompute();
    bool createSurfaceDescriptorPool(uint32_t maxFramesInFlight);
    bool createSurfaceDescriptorSetLayout();
    bool createSurfacePipeline();
    bool prepareVoronoiRuntime();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    VoronoiSystemRuntime runtime;
    VoronoiSurfaceRuntime surfaceRuntime;

    std::unique_ptr<VoronoiRenderer> voronoiRenderer;
    std::unique_ptr<PointRenderer> pointRenderer;
    std::unique_ptr<VoronoiGeoCompute> voronoiGeoCompute;
    std::unique_ptr<VoronoiCandidateCompute> voronoiCandidateCompute;
    std::unique_ptr<VoronoiSurfaceStage> surfaceStage;
    VoronoiBuilder voronoiBuilder;

    uint32_t maxFramesInFlight;
    bool initialized = false;
    bool debugEnable = false;
    static constexpr int K_NEIGHBORS = 50;
};

