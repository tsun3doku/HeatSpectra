#pragma once

#include "mesh/remesher/SupportingHalfedge.hpp"
#include "voronoi/VoronoiBuilder.hpp"
#include "voronoi/VoronoiDomain.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"
#include "voronoi/VoronoiResources.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

class CommandPool;
class MemoryAllocator;
class ModelRegistry;
class VulkanDevice;
class VoronoiCandidateCompute;
class VoronoiGeoCompute;

class VoronoiSystemRuntime {
public:
    bool isReady() const { return voronoiReady; }
    bool isSeederReady() const { return voronoiSeederReady; }

    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& getModelRuntimes() const { return modelRuntimes; }
    const std::vector<VoronoiDomain>& getReceiverVoronoiDomains() const { return receiverVoronoiDomains; }
    std::vector<VoronoiDomain>& receiverVoronoiDomainsRef() { return receiverVoronoiDomains; }
    const VoronoiDomain* findReceiverDomain(uint32_t receiverModelId) const;

    uint32_t getVoronoiNodeCount() const { return resources.voronoiNodeCount; }

    VoronoiResources& resourcesRef() { return resources; }
    const VoronoiResources& resourcesRef() const { return resources; }

    void setReceiverGeometry(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool,
        const std::vector<uint32_t>& receiverNodeModelIds,
        const std::vector<std::vector<glm::vec3>>& receiverGeometryPositions,
        const std::vector<std::vector<uint32_t>>& receiverGeometryTriangleIndices,
        const std::vector<SupportingHalfedge::IntrinsicMesh>& receiverIntrinsicMeshes,
        const std::vector<std::vector<VoronoiModelRuntime::SurfaceVertex>>& receiverSurfaceVertices,
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
    void clearReceiverGeometry();
    void setParams(float cellSize, int voxelResolution);
    float getCellSize() const { return cellSize; }
    int getVoxelResolution() const { return voxelResolution; }
    void markSeederReady();
    void markReady();
    void uploadModelStagingBuffers(CommandPool& renderCommandPool);

    void cleanupResources(VulkanDevice& vulkanDevice);
    void cleanup(MemoryAllocator& memoryAllocator);

private:
    void invalidateMaterialization();
    void clearReceiverDomains();

    std::vector<std::unique_ptr<VoronoiModelRuntime>> modelRuntimes;
    std::vector<uint32_t> activeReceiverModelIds;
    float cellSize = 0.005f;
    int voxelResolution = 128;
    std::vector<VoronoiDomain> receiverVoronoiDomains;
    VoronoiResources resources;
    bool voronoiSeederReady = false;
    bool voronoiReady = false;
    static constexpr float AMBIENT_TEMPERATURE = 1.0f;
};
