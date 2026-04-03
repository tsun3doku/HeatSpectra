#pragma once

#include "domain/VoronoiParams.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "voronoi/VoronoiBuilder.hpp"
#include "voronoi/VoronoiDomain.hpp"
#include "voronoi/VoronoiGeometryRuntime.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"
#include "voronoi/VoronoiSystemResources.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

class CommandPool;
class MemoryAllocator;
class PointRenderer;
class ResourceManager;
class VulkanDevice;
class VoronoiCandidateCompute;
class VoronoiGeoCompute;
class VoronoiSurfaceRuntime;

class VoronoiSystemRuntime {
public:
    bool isReady() const { return voronoiReady; }
    bool isSeederReady() const { return voronoiSeederReady; }

    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& getModelRuntimes() const { return modelRuntimes; }
    const std::vector<VoronoiDomain>& getReceiverVoronoiDomains() const { return receiverVoronoiDomains; }
    const VoronoiDomain* findReceiverDomain(uint32_t receiverModelId) const;

    uint32_t getVoronoiNodeCount() const { return resources.voronoi.voronoiNodeCount; }

    VoronoiSystemResources& resourcesRef() { return resources; }
    const VoronoiSystemResources& resourcesRef() const { return resources; }
    VoronoiResources& voronoiResourcesRef() { return resources.voronoi; }
    const VoronoiResources& voronoiResourcesRef() const { return resources.voronoi; }

    void setReceiverPayloads(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool,
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

    bool prepare(
        VoronoiBuilder& voronoiBuilder,
        bool debugEnable,
        uint32_t maxNeighbors,
        VoronoiGeoCompute* voronoiGeoCompute,
        PointRenderer* pointRenderer);
    void executeBufferTransfers(
        CommandPool& renderCommandPool,
        VoronoiSurfaceRuntime& surfaceRuntime,
        VoronoiCandidateCompute* voronoiCandidateCompute);

    void cleanupResources(VulkanDevice& vulkanDevice);
    void cleanup(MemoryAllocator& memoryAllocator);

private:
    void invalidateMaterialization();
    void clearReceiverDomains();
    void uploadModelStagingBuffers(CommandPool& renderCommandPool);
    void dispatchVoronoiCandidateUpdates(const VoronoiSurfaceRuntime& surfaceRuntime, VoronoiCandidateCompute* voronoiCandidateCompute);

    std::vector<std::unique_ptr<VoronoiModelRuntime>> modelRuntimes;
    std::vector<uint32_t> activeReceiverNodeModelIds;
    std::vector<std::vector<glm::vec3>> activeReceiverGeometryPositions;
    std::vector<std::vector<uint32_t>> activeReceiverGeometryTriangleIndices;
    std::vector<SupportingHalfedge::IntrinsicMesh> activeReceiverIntrinsicMeshes;
    std::vector<std::vector<VoronoiGeometryRuntime::SurfaceVertex>> activeReceiverSurfaceVertices;
    std::vector<std::vector<uint32_t>> activeReceiverIntrinsicTriangleIndices;
    std::vector<uint32_t> activeReceiverModelIds;
    std::vector<VkBuffer> activeMeshVertexBuffers;
    std::vector<VkDeviceSize> activeMeshVertexBufferOffsets;
    std::vector<VkBuffer> activeMeshIndexBuffers;
    std::vector<VkDeviceSize> activeMeshIndexBufferOffsets;
    std::vector<uint32_t> activeMeshIndexCounts;
    std::vector<glm::mat4> activeMeshModelMatrices;
    std::vector<VkBufferView> activeSupportingHalfedgeViews;
    std::vector<VkBufferView> activeSupportingAngleViews;
    std::vector<VkBufferView> activeHalfedgeViews;
    std::vector<VkBufferView> activeEdgeViews;
    std::vector<VkBufferView> activeTriangleViews;
    std::vector<VkBufferView> activeLengthViews;
    std::vector<VkBufferView> activeInputHalfedgeViews;
    std::vector<VkBufferView> activeInputEdgeViews;
    std::vector<VkBufferView> activeInputTriangleViews;
    std::vector<VkBufferView> activeInputLengthViews;
    VoronoiParams voronoiParams;
    std::vector<VoronoiDomain> receiverVoronoiDomains;
    VoronoiSystemResources resources;
    bool voronoiSeederReady = false;
    bool voronoiReady = false;
    static constexpr float AMBIENT_TEMPERATURE = 1.0f;
};
