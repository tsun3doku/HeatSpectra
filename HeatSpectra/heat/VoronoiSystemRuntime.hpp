#pragma once

#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"
#include "domain/VoronoiParams.hpp"
#include "voronoi/VoronoiBuilder.hpp"
#include "voronoi/VoronoiDomain.hpp"
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
class RuntimeIntrinsicCache;
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
        ResourceManager& resourceManager,
        const RuntimeIntrinsicCache& intrinsicCache,
        CommandPool& renderCommandPool,
        const std::vector<GeometryData>& receiverGeometries,
        const std::vector<IntrinsicMeshData>& receiverIntrinsics,
        const std::vector<uint32_t>& receiverModelIds);
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
    static bool isSameGeometryAttribute(const GeometryAttribute& lhs, const GeometryAttribute& rhs);
    static bool isSameGeometryGroup(const GeometryGroup& lhs, const GeometryGroup& rhs);
    static bool isSameGeometryData(const GeometryData& lhs, const GeometryData& rhs);
    static bool isSameIntrinsicVertexData(const IntrinsicMeshVertexData& lhs, const IntrinsicMeshVertexData& rhs);
    static bool isSameIntrinsicTriangleData(const IntrinsicMeshTriangleData& lhs, const IntrinsicMeshTriangleData& rhs);
    static bool isSameIntrinsicMeshData(const IntrinsicMeshData& lhs, const IntrinsicMeshData& rhs);
    static bool haveSameReceiverPayloads(
        const std::vector<GeometryData>& lhsGeometries,
        const std::vector<IntrinsicMeshData>& lhsIntrinsics,
        const std::vector<uint32_t>& lhsModelIds,
        const std::vector<GeometryData>& rhsGeometries,
        const std::vector<IntrinsicMeshData>& rhsIntrinsics,
        const std::vector<uint32_t>& rhsModelIds);
    void uploadModelStagingBuffers(CommandPool& renderCommandPool);
    void dispatchVoronoiCandidateUpdates(const VoronoiSurfaceRuntime& surfaceRuntime, VoronoiCandidateCompute* voronoiCandidateCompute);

    std::vector<std::unique_ptr<VoronoiModelRuntime>> modelRuntimes;
    std::vector<GeometryData> activeReceiverGeometries;
    std::vector<IntrinsicMeshData> activeReceiverIntrinsics;
    std::vector<uint32_t> activeReceiverModelIds;
    VoronoiParams voronoiParams;
    std::vector<VoronoiDomain> receiverVoronoiDomains;
    VoronoiSystemResources resources;
    bool voronoiSeederReady = false;
    bool voronoiReady = false;
    static constexpr float AMBIENT_TEMPERATURE = 1.0f;
};
