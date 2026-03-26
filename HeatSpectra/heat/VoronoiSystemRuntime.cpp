#include "VoronoiSystemRuntime.hpp"

#include "runtime/RuntimeIntrinsicCache.hpp"
#include "scene/Model.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiCandidateCompute.hpp"
#include "voronoi/VoronoiGeometryRuntime.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"
#include "voronoi/VoronoiSurfaceRuntime.hpp"

#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>

std::string formatReceiverModelIds(const std::vector<uint32_t>& receiverModelIds) {
    std::ostringstream stream;
    stream << "[";
    for (size_t index = 0; index < receiverModelIds.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << receiverModelIds[index];
    }
    stream << "]";
    return stream.str();
}

const VoronoiDomain* VoronoiSystemRuntime::findReceiverDomain(uint32_t receiverModelId) const {
    if (receiverModelId == 0) {
        return nullptr;
    }

    for (const VoronoiDomain& domain : receiverVoronoiDomains) {
        if (domain.receiverModelId == receiverModelId) {
            return &domain;
        }
    }

    return nullptr;
}

void VoronoiSystemRuntime::invalidateMaterialization() {
    voronoiReady = false;
    voronoiSeederReady = false;
}

bool VoronoiSystemRuntime::isSameGeometryAttribute(const GeometryAttribute& lhs, const GeometryAttribute& rhs) {
    return lhs.name == rhs.name &&
        lhs.domain == rhs.domain &&
        lhs.dataType == rhs.dataType &&
        lhs.tupleSize == rhs.tupleSize &&
        lhs.floatValues == rhs.floatValues &&
        lhs.intValues == rhs.intValues &&
        lhs.boolValues == rhs.boolValues;
}

bool VoronoiSystemRuntime::isSameGeometryGroup(const GeometryGroup& lhs, const GeometryGroup& rhs) {
    return lhs.id == rhs.id &&
        lhs.name == rhs.name &&
        lhs.source == rhs.source;
}

bool VoronoiSystemRuntime::isSameGeometryData(const GeometryData& lhs, const GeometryData& rhs) {
    const bool sameIntrinsicHandle =
        lhs.intrinsicHandle.key == rhs.intrinsicHandle.key &&
        lhs.intrinsicHandle.revision == rhs.intrinsicHandle.revision &&
        lhs.intrinsicHandle.count == rhs.intrinsicHandle.count;

    if (lhs.baseModelPath != rhs.baseModelPath ||
        lhs.modelId != rhs.modelId ||
        lhs.geometryRevision != rhs.geometryRevision ||
        !sameIntrinsicHandle ||
        lhs.localToWorld != rhs.localToWorld ||
        lhs.pointPositions != rhs.pointPositions ||
        lhs.triangleIndices != rhs.triangleIndices ||
        lhs.triangleGroupIds != rhs.triangleGroupIds ||
        lhs.groups.size() != rhs.groups.size() ||
        lhs.attributes.size() != rhs.attributes.size()) {
        return false;
    }

    for (size_t index = 0; index < lhs.groups.size(); ++index) {
        if (!isSameGeometryGroup(lhs.groups[index], rhs.groups[index])) {
            return false;
        }
    }

    for (size_t index = 0; index < lhs.attributes.size(); ++index) {
        if (!isSameGeometryAttribute(lhs.attributes[index], rhs.attributes[index])) {
            return false;
        }
    }

    return true;
}

bool VoronoiSystemRuntime::isSameIntrinsicVertexData(const IntrinsicMeshVertexData& lhs, const IntrinsicMeshVertexData& rhs) {
    return lhs.intrinsicVertexId == rhs.intrinsicVertexId &&
        lhs.position[0] == rhs.position[0] &&
        lhs.position[1] == rhs.position[1] &&
        lhs.position[2] == rhs.position[2] &&
        lhs.normal[0] == rhs.normal[0] &&
        lhs.normal[1] == rhs.normal[1] &&
        lhs.normal[2] == rhs.normal[2] &&
        lhs.inputLocationType == rhs.inputLocationType &&
        lhs.inputElementId == rhs.inputElementId &&
        lhs.inputBaryCoords[0] == rhs.inputBaryCoords[0] &&
        lhs.inputBaryCoords[1] == rhs.inputBaryCoords[1] &&
        lhs.inputBaryCoords[2] == rhs.inputBaryCoords[2];
}

bool VoronoiSystemRuntime::isSameIntrinsicTriangleData(const IntrinsicMeshTriangleData& lhs, const IntrinsicMeshTriangleData& rhs) {
    return lhs.center[0] == rhs.center[0] &&
        lhs.center[1] == rhs.center[1] &&
        lhs.center[2] == rhs.center[2] &&
        lhs.normal[0] == rhs.normal[0] &&
        lhs.normal[1] == rhs.normal[1] &&
        lhs.normal[2] == rhs.normal[2] &&
        lhs.area == rhs.area &&
        lhs.vertexIndices[0] == rhs.vertexIndices[0] &&
        lhs.vertexIndices[1] == rhs.vertexIndices[1] &&
        lhs.vertexIndices[2] == rhs.vertexIndices[2] &&
        lhs.faceId == rhs.faceId;
}

bool VoronoiSystemRuntime::isSameIntrinsicMeshData(const IntrinsicMeshData& lhs, const IntrinsicMeshData& rhs) {
    if (lhs.vertices.size() != rhs.vertices.size() ||
        lhs.triangleIndices != rhs.triangleIndices ||
        lhs.faceIds != rhs.faceIds ||
        lhs.triangles.size() != rhs.triangles.size() ||
        lhs.supportingHalfedges != rhs.supportingHalfedges ||
        lhs.supportingAngles != rhs.supportingAngles ||
        lhs.intrinsicHalfedges != rhs.intrinsicHalfedges ||
        lhs.intrinsicEdges != rhs.intrinsicEdges ||
        lhs.intrinsicTriangles != rhs.intrinsicTriangles ||
        lhs.intrinsicEdgeLengths != rhs.intrinsicEdgeLengths ||
        lhs.inputHalfedges != rhs.inputHalfedges ||
        lhs.inputEdges != rhs.inputEdges ||
        lhs.inputTriangles != rhs.inputTriangles ||
        lhs.inputEdgeLengths != rhs.inputEdgeLengths) {
        return false;
    }

    for (size_t index = 0; index < lhs.vertices.size(); ++index) {
        if (!isSameIntrinsicVertexData(lhs.vertices[index], rhs.vertices[index])) {
            return false;
        }
    }

    for (size_t index = 0; index < lhs.triangles.size(); ++index) {
        if (!isSameIntrinsicTriangleData(lhs.triangles[index], rhs.triangles[index])) {
            return false;
        }
    }

    return true;
}

bool VoronoiSystemRuntime::haveSameReceiverPayloads(
    const std::vector<GeometryData>& lhsGeometries,
    const std::vector<IntrinsicMeshData>& lhsIntrinsics,
    const std::vector<uint32_t>& lhsModelIds,
    const std::vector<GeometryData>& rhsGeometries,
    const std::vector<IntrinsicMeshData>& rhsIntrinsics,
    const std::vector<uint32_t>& rhsModelIds) {
    if (lhsGeometries.size() != rhsGeometries.size() ||
        lhsIntrinsics.size() != rhsIntrinsics.size() ||
        lhsModelIds != rhsModelIds) {
        return false;
    }

    for (size_t index = 0; index < lhsGeometries.size(); ++index) {
        if (!isSameGeometryData(lhsGeometries[index], rhsGeometries[index])) {
            return false;
        }
    }

    for (size_t index = 0; index < lhsIntrinsics.size(); ++index) {
        if (!isSameIntrinsicMeshData(lhsIntrinsics[index], rhsIntrinsics[index])) {
            return false;
        }
    }

    return true;
}

void VoronoiSystemRuntime::setReceiverPayloads(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ResourceManager& resourceManager,
    const RuntimeIntrinsicCache& intrinsicCache,
    CommandPool& renderCommandPool,
    const std::vector<GeometryData>& receiverGeometries,
    const std::vector<IntrinsicMeshData>& receiverIntrinsics,
    const std::vector<uint32_t>& receiverModelIds) {
    if (haveSameReceiverPayloads(
            activeReceiverGeometries,
            activeReceiverIntrinsics,
            activeReceiverModelIds,
            receiverGeometries,
            receiverIntrinsics,
            receiverModelIds)) {
        return;
    }

    if (activeReceiverModelIds != receiverModelIds) {
        std::cout << "[VoronoiSystemRuntime] Rebuilding receiver runtimes: oldCount="
                  << activeReceiverModelIds.size()
                  << ", newCount=" << receiverModelIds.size()
                  << " oldIds=" << formatReceiverModelIds(activeReceiverModelIds)
                  << " newIds=" << formatReceiverModelIds(receiverModelIds)
                  << std::endl;
    }

    activeReceiverGeometries = receiverGeometries;
    activeReceiverIntrinsics = receiverIntrinsics;
    activeReceiverModelIds = receiverModelIds;
    clearReceiverDomains();
    invalidateMaterialization();

    for (auto& modelRuntime : modelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    modelRuntimes.clear();

    std::unordered_set<uint32_t> seenReceiverIds;
    for (std::size_t index = 0; index < receiverGeometries.size(); ++index) {
        const uint32_t receiverId = receiverModelIds[index];
        if (receiverId == 0 || !seenReceiverIds.insert(receiverId).second) {
            continue;
        }

        const GeometryData& geometry = receiverGeometries[index];
        if (geometry.intrinsicHandle.key == 0) {
            continue;
        }

        Model* receiverModel = resourceManager.getModelByID(receiverId);
        if (!receiverModel) {
            continue;
        }
        const RuntimeIntrinsicCache::Entry* intrinsicEntry = intrinsicCache.get(geometry.intrinsicHandle);
        if (!intrinsicEntry) {
            continue;
        }

        auto modelRuntime = std::make_unique<VoronoiModelRuntime>(
            vulkanDevice,
            memoryAllocator,
            *receiverModel,
            geometry,
            receiverIntrinsics[index],
            *intrinsicEntry,
            renderCommandPool);
        if (!modelRuntime->createVoronoiBuffers()) {
            std::cout << "[VoronoiSystemRuntime] Failed to create Voronoi buffers for runtimeModelId="
                      << receiverId << std::endl;
            modelRuntime->cleanup();
            continue;
        }

        std::cout << "[VoronoiSystemRuntime] Created receiver runtime for runtimeModelId="
                  << receiverId
                  << " intrinsicVertices=" << modelRuntime->getIntrinsicVertexCount()
                  << " intrinsicTriangles=" << modelRuntime->getIntrinsicTriangleCount()
                  << std::endl;
        modelRuntimes.push_back(std::move(modelRuntime));
    }

    std::cout << "[VoronoiSystemRuntime] Receiver runtime rebuild complete"
              << " activeIds=" << formatReceiverModelIds(activeReceiverModelIds)
              << " modelRuntimeCount=" << modelRuntimes.size()
              << std::endl;
}

void VoronoiSystemRuntime::clearReceiverPayloads() {
    if (activeReceiverModelIds.empty() && modelRuntimes.empty()) {
        return;
    }

    std::cout << "[VoronoiSystemRuntime] Clearing receiver payloads"
              << " activeIds=" << formatReceiverModelIds(activeReceiverModelIds)
              << " modelRuntimeCount=" << modelRuntimes.size()
              << std::endl;

    clearReceiverDomains();
    invalidateMaterialization();
    activeReceiverGeometries.clear();
    activeReceiverIntrinsics.clear();
    activeReceiverModelIds.clear();

    for (auto& modelRuntime : modelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    modelRuntimes.clear();
}

void VoronoiSystemRuntime::setParams(const VoronoiParams& params) {
    if (voronoiParams == params) {
        return;
    }

    voronoiParams = params;
    clearReceiverDomains();
    invalidateMaterialization();
}

void VoronoiSystemRuntime::clearReceiverDomains() {
    receiverVoronoiDomains.clear();
}

bool VoronoiSystemRuntime::prepare(
    VoronoiBuilder& voronoiBuilder,
    bool debugEnable,
    uint32_t maxNeighbors,
    VoronoiGeoCompute* voronoiGeoCompute,
    PointRenderer* pointRenderer) {
    if (!voronoiBuilder.buildDomains(modelRuntimes, receiverVoronoiDomains, voronoiParams, maxNeighbors)) {
        std::cerr << "[VoronoiSystemRuntime] Failed to build Voronoi domains" << std::endl;
        return false;
    }

    size_t totalSeedCount = 0;
    for (const VoronoiDomain& domain : receiverVoronoiDomains) {
        totalSeedCount += domain.seedFlags.size();
        std::cout << "[VoronoiSystemRuntime] Receiver domain built for runtimeModelId="
                  << domain.receiverModelId
                  << " nodeOffset=" << domain.nodeOffset
                  << " nodeCount=" << domain.nodeCount
                  << " seedFlags=" << domain.seedFlags.size()
                  << " voxelGridBuilt=" << (domain.voxelGridBuilt ? "true" : "false")
                  << std::endl;
    }
    std::cout << "[VoronoiSystemRuntime] Receiver domains ready"
              << " count=" << receiverVoronoiDomains.size()
              << " totalSeeds=" << totalSeedCount
              << std::endl;

    voronoiSeederReady = true;

    if (!voronoiBuilder.generateDiagram(
            receiverVoronoiDomains,
            debugEnable,
            maxNeighbors,
            voronoiGeoCompute,
            pointRenderer)) {
        std::cerr << "[VoronoiSystemRuntime] Voronoi generation failed" << std::endl;
        return false;
    }

    if (resources.voronoi.voronoiNodeCount == 0) {
        std::cerr << "[VoronoiSystemRuntime] Voronoi generation produced zero nodes" << std::endl;
        return false;
    }

    if (!voronoiBuilder.stageSurfaceMappings(receiverVoronoiDomains, maxNeighbors)) {
        return false;
    }

    std::cout << "[VoronoiSystemRuntime] Voronoi runtime ready with nodeCount="
              << resources.voronoi.voronoiNodeCount
              << " receiverDomains=" << receiverVoronoiDomains.size()
              << std::endl;

    voronoiReady = true;
    return true;
}

void VoronoiSystemRuntime::executeBufferTransfers(
    CommandPool& renderCommandPool,
    VoronoiSurfaceRuntime& surfaceRuntime,
    VoronoiCandidateCompute* voronoiCandidateCompute) {
    std::cout << "[VoronoiSystemRuntime] Executing buffer transfers"
              << " geometryRuntimes=" << surfaceRuntime.getGeometryRuntimes().size()
              << " modelRuntimes=" << modelRuntimes.size()
              << " nodeCount=" << resources.voronoi.voronoiNodeCount
              << std::endl;
    surfaceRuntime.executeBufferTransfers(renderCommandPool);
    uploadModelStagingBuffers(renderCommandPool);
    dispatchVoronoiCandidateUpdates(surfaceRuntime, voronoiCandidateCompute);
    std::cout << "[VoronoiSystemRuntime] Buffer transfers complete" << std::endl;
}

void VoronoiSystemRuntime::uploadModelStagingBuffers(CommandPool& renderCommandPool) {
    VkCommandBuffer copyCmd = renderCommandPool.beginCommands();
    for (auto& modelRuntime : modelRuntimes) {
        modelRuntime->executeBufferTransfers(copyCmd);
    }

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        copyCmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        1, &memBarrier,
        0, nullptr,
        0, nullptr);

    renderCommandPool.endCommands(copyCmd);

    for (auto& modelRuntime : modelRuntimes) {
        modelRuntime->cleanupStagingBuffers();
    }
}

void VoronoiSystemRuntime::dispatchVoronoiCandidateUpdates(const VoronoiSurfaceRuntime& surfaceRuntime, VoronoiCandidateCompute* voronoiCandidateCompute) {
    if (!voronoiCandidateCompute || resources.voronoi.voronoiNodeCount == 0) {
        std::cout << "[VoronoiSystemRuntime] Skipping Voronoi candidate updates"
                  << " compute=" << (voronoiCandidateCompute ? "present" : "missing")
                  << " nodeCount=" << resources.voronoi.voronoiNodeCount
                  << std::endl;
        return;
    }

    const auto& geometryRuntimes = surfaceRuntime.getGeometryRuntimes();
    size_t dispatchedCount = 0;
    size_t skippedMissingDomain = 0;
    size_t skippedMissingGeometryRuntime = 0;
    size_t skippedZeroFaces = 0;
    size_t skippedMissingCandidateBuffer = 0;
    for (const auto& modelRuntime : modelRuntimes) {
        const uint32_t receiverModelId = modelRuntime->getModel().getRuntimeModelId();
        const VoronoiDomain* receiverDomain = findReceiverDomain(receiverModelId);
        if (!receiverDomain || receiverDomain->nodeCount == 0) {
            ++skippedMissingDomain;
            continue;
        }

        const VoronoiGeometryRuntime* geometryRuntime = nullptr;
        for (const auto& geometry : geometryRuntimes) {
            if (geometry && geometry->getModel().getRuntimeModelId() == receiverModelId) {
                geometryRuntime = geometry.get();
                break;
            }
        }
        uint32_t faceCount = static_cast<uint32_t>(modelRuntime->getIntrinsicTriangleCount());
        if (!geometryRuntime) {
            ++skippedMissingGeometryRuntime;
            continue;
        }
        if (faceCount == 0) {
            ++skippedZeroFaces;
            continue;
        }
        if (modelRuntime->getVoronoiCandidateBuffer() == VK_NULL_HANDLE) {
            ++skippedMissingCandidateBuffer;
            continue;
        }

        VoronoiCandidateCompute::Bindings bindings{};
        bindings.vertexBuffer = geometryRuntime->getSurfaceBuffer();
        bindings.vertexBufferOffset = geometryRuntime->getSurfaceBufferOffset();
        bindings.faceIndexBuffer = modelRuntime->getTriangleIndicesBuffer();
        bindings.faceIndexBufferOffset = modelRuntime->getTriangleIndicesBufferOffset();
        bindings.seedPositionBuffer = resources.voronoi.seedPositionBuffer;
        bindings.seedPositionBufferOffset = resources.voronoi.seedPositionBufferOffset;
        bindings.candidateBuffer = modelRuntime->getVoronoiCandidateBuffer();
        bindings.candidateBufferOffset = modelRuntime->getVoronoiCandidateBufferOffset();

        voronoiCandidateCompute->updateDescriptors(bindings);
        voronoiCandidateCompute->dispatch(faceCount, receiverDomain->nodeCount, receiverDomain->nodeOffset);
        ++dispatchedCount;
        std::cout << "[VoronoiSystemRuntime] Dispatched Voronoi candidate update for runtimeModelId="
                  << receiverModelId
                  << " faces=" << faceCount
                  << " domainNodeCount=" << receiverDomain->nodeCount
                  << " nodeOffset=" << receiverDomain->nodeOffset
                  << std::endl;
    }

    std::cout << "[VoronoiSystemRuntime] Voronoi candidate update summary"
              << " dispatched=" << dispatchedCount
              << " skippedMissingDomain=" << skippedMissingDomain
              << " skippedMissingGeometryRuntime=" << skippedMissingGeometryRuntime
              << " skippedZeroFaces=" << skippedZeroFaces
              << " skippedMissingCandidateBuffer=" << skippedMissingCandidateBuffer
              << std::endl;
}

void VoronoiSystemRuntime::cleanupResources(VulkanDevice& vulkanDevice) {
    if (resources.surfacePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), resources.surfacePipeline, nullptr);
        resources.surfacePipeline = VK_NULL_HANDLE;
    }
    if (resources.surfacePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), resources.surfacePipelineLayout, nullptr);
        resources.surfacePipelineLayout = VK_NULL_HANDLE;
    }
    if (resources.surfaceDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), resources.surfaceDescriptorPool, nullptr);
        resources.surfaceDescriptorPool = VK_NULL_HANDLE;
    }
    if (resources.surfaceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), resources.surfaceDescriptorSetLayout, nullptr);
        resources.surfaceDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VoronoiSystemRuntime::cleanup(MemoryAllocator& memoryAllocator) {
    clearReceiverDomains();
    invalidateMaterialization();

    auto freeBuffer = [&](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    freeBuffer(resources.voronoi.voronoiNodeBuffer, resources.voronoi.voronoiNodeBufferOffset);
    resources.voronoi.mappedVoronoiNodeData = nullptr;
    freeBuffer(resources.voronoi.voronoiNeighborBuffer, resources.voronoi.voronoiNeighborBufferOffset);
    freeBuffer(resources.voronoi.neighborIndicesBuffer, resources.voronoi.neighborIndicesBufferOffset);
    freeBuffer(resources.voronoi.interfaceAreasBuffer, resources.voronoi.interfaceAreasBufferOffset);
    resources.voronoi.mappedInterfaceAreasData = nullptr;
    freeBuffer(resources.voronoi.interfaceNeighborIdsBuffer, resources.voronoi.interfaceNeighborIdsBufferOffset);
    resources.voronoi.mappedInterfaceNeighborIdsData = nullptr;
    freeBuffer(resources.voronoi.meshTriangleBuffer, resources.voronoi.meshTriangleBufferOffset);
    freeBuffer(resources.voronoi.seedPositionBuffer, resources.voronoi.seedPositionBufferOffset);
    resources.voronoi.mappedSeedPositionData = nullptr;
    freeBuffer(resources.voronoi.seedFlagsBuffer, resources.voronoi.seedFlagsBufferOffset);
    resources.voronoi.mappedSeedFlagsData = nullptr;
    freeBuffer(resources.voronoi.debugCellGeometryBuffer, resources.voronoi.debugCellGeometryBufferOffset);
    resources.voronoi.mappedDebugCellGeometryData = nullptr;
    freeBuffer(resources.voronoi.voronoiDumpBuffer, resources.voronoi.voronoiDumpBufferOffset);
    resources.voronoi.mappedVoronoiDumpData = nullptr;
    freeBuffer(resources.voronoi.voxelGridParamsBuffer, resources.voronoi.voxelGridParamsBufferOffset);
    freeBuffer(resources.voronoi.voxelOccupancyBuffer, resources.voronoi.voxelOccupancyBufferOffset);
    freeBuffer(resources.voronoi.voxelTrianglesListBuffer, resources.voronoi.voxelTrianglesListBufferOffset);
    freeBuffer(resources.voronoi.voxelOffsetsBuffer, resources.voronoi.voxelOffsetsBufferOffset);
    resources.voronoi.voronoiNodeCount = 0;

    for (auto& modelRuntime : modelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    modelRuntimes.clear();
}
