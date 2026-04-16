#include "VoronoiSystemRuntime.hpp"

#include <algorithm>

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
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

void VoronoiSystemRuntime::setReceiverPayloads(
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
    const std::vector<VkBufferView>& inputLengthViews) {
    if (activeReceiverModelIds != receiverModelIds) {
        std::cout << "[VoronoiSystemRuntime] Rebuilding receiver runtimes: oldCount="
                  << activeReceiverModelIds.size()
                  << ", newCount=" << receiverModelIds.size()
                  << " oldIds=" << formatReceiverModelIds(activeReceiverModelIds)
                  << " newIds=" << formatReceiverModelIds(receiverModelIds)
                  << std::endl;
    }

    activeReceiverNodeModelIds = receiverNodeModelIds;
    activeReceiverGeometryPositions = receiverGeometryPositions;
    activeReceiverGeometryTriangleIndices = receiverGeometryTriangleIndices;
    activeReceiverIntrinsicMeshes = receiverIntrinsicMeshes;
    activeReceiverSurfaceVertices = receiverSurfaceVertices;
    activeReceiverIntrinsicTriangleIndices = receiverIntrinsicTriangleIndices;
    activeReceiverModelIds = receiverModelIds;
    activeMeshVertexBuffers = meshVertexBuffers;
    activeMeshVertexBufferOffsets = meshVertexBufferOffsets;
    activeMeshIndexBuffers = meshIndexBuffers;
    activeMeshIndexBufferOffsets = meshIndexBufferOffsets;
    activeMeshIndexCounts = meshIndexCounts;
    activeMeshModelMatrices = meshModelMatrices;
    activeSupportingHalfedgeViews = supportingHalfedgeViews;
    activeSupportingAngleViews = supportingAngleViews;
    activeHalfedgeViews = halfedgeViews;
    activeEdgeViews = edgeViews;
    activeTriangleViews = triangleViews;
    activeLengthViews = lengthViews;
    activeInputHalfedgeViews = inputHalfedgeViews;
    activeInputEdgeViews = inputEdgeViews;
    activeInputTriangleViews = inputTriangleViews;
    activeInputLengthViews = inputLengthViews;
    clearReceiverDomains();
    invalidateMaterialization();

    for (auto& modelRuntime : modelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    modelRuntimes.clear();

    const size_t receiverCount = std::min({
        receiverNodeModelIds.size(),
        receiverGeometryPositions.size(),
        receiverGeometryTriangleIndices.size(),
        receiverIntrinsicMeshes.size(),
        receiverSurfaceVertices.size(),
        receiverIntrinsicTriangleIndices.size(),
        receiverModelIds.size(),
        meshVertexBuffers.size(),
        meshVertexBufferOffsets.size(),
        meshIndexBuffers.size(),
        meshIndexBufferOffsets.size(),
        meshIndexCounts.size(),
        meshModelMatrices.size(),
        supportingHalfedgeViews.size(),
        supportingAngleViews.size(),
        halfedgeViews.size(),
        edgeViews.size(),
        triangleViews.size(),
        lengthViews.size(),
        inputHalfedgeViews.size(),
        inputEdgeViews.size(),
        inputTriangleViews.size(),
        inputLengthViews.size()
    });

    std::unordered_set<uint32_t> seenReceiverIds;
    for (std::size_t index = 0; index < receiverCount; ++index) {
        const uint32_t receiverId = receiverModelIds[index];
        if (receiverId == 0 || !seenReceiverIds.insert(receiverId).second) {
            continue;
        }

        if (supportingHalfedgeViews[index] == VK_NULL_HANDLE ||
            supportingAngleViews[index] == VK_NULL_HANDLE ||
            halfedgeViews[index] == VK_NULL_HANDLE ||
            edgeViews[index] == VK_NULL_HANDLE ||
            triangleViews[index] == VK_NULL_HANDLE ||
            lengthViews[index] == VK_NULL_HANDLE ||
            inputHalfedgeViews[index] == VK_NULL_HANDLE ||
            inputEdgeViews[index] == VK_NULL_HANDLE ||
            inputTriangleViews[index] == VK_NULL_HANDLE ||
            inputLengthViews[index] == VK_NULL_HANDLE) {
            continue;
        }
        if (meshVertexBuffers[index] == VK_NULL_HANDLE ||
            meshIndexBuffers[index] == VK_NULL_HANDLE ||
            meshIndexCounts[index] == 0) {
            continue;
        }

        std::vector<glm::vec3> intrinsicSurfacePositions;
        intrinsicSurfacePositions.reserve(receiverSurfaceVertices[index].size());
        for (const VoronoiGeometryRuntime::SurfaceVertex& vertex : receiverSurfaceVertices[index]) {
            intrinsicSurfacePositions.push_back(vertex.position);
        }

        auto modelRuntime = std::make_unique<VoronoiModelRuntime>(
            vulkanDevice,
            memoryAllocator,
            receiverId,
            meshVertexBuffers[index],
            meshVertexBufferOffsets[index],
            meshIndexBuffers[index],
            meshIndexBufferOffsets[index],
            meshIndexCounts[index],
            meshModelMatrices[index],
            VoronoiModelRuntime::CpuData{
                receiverNodeModelIds[index],
                receiverIntrinsicMeshes[index],
                receiverGeometryPositions[index],
                receiverGeometryTriangleIndices[index],
                std::move(intrinsicSurfacePositions),
                receiverIntrinsicTriangleIndices[index]
            },
            supportingHalfedgeViews[index],
            supportingAngleViews[index],
            halfedgeViews[index],
            edgeViews[index],
            triangleViews[index],
            lengthViews[index],
            inputHalfedgeViews[index],
            inputEdgeViews[index],
            inputTriangleViews[index],
            inputLengthViews[index],
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
    activeReceiverNodeModelIds.clear();
    activeReceiverGeometryPositions.clear();
    activeReceiverGeometryTriangleIndices.clear();
    activeReceiverIntrinsicMeshes.clear();
    activeReceiverSurfaceVertices.clear();
    activeReceiverIntrinsicTriangleIndices.clear();
    activeReceiverModelIds.clear();
    activeMeshVertexBuffers.clear();
    activeMeshVertexBufferOffsets.clear();
    activeMeshIndexBuffers.clear();
    activeMeshIndexBufferOffsets.clear();
    activeMeshIndexCounts.clear();
    activeMeshModelMatrices.clear();
    activeSupportingHalfedgeViews.clear();
    activeSupportingAngleViews.clear();
    activeHalfedgeViews.clear();
    activeEdgeViews.clear();
    activeTriangleViews.clear();
    activeLengthViews.clear();
    activeInputHalfedgeViews.clear();
    activeInputEdgeViews.clear();
    activeInputTriangleViews.clear();
    activeInputLengthViews.clear();

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
    VoronoiGeoCompute* voronoiGeoCompute) {
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
            voronoiGeoCompute)) {
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
        const uint32_t receiverModelId = modelRuntime->getRuntimeModelId();
        const VoronoiDomain* receiverDomain = findReceiverDomain(receiverModelId);
        if (!receiverDomain || receiverDomain->nodeCount == 0) {
            ++skippedMissingDomain;
            continue;
        }

        const VoronoiGeometryRuntime* geometryRuntime = nullptr;
        for (const auto& geometry : geometryRuntimes) {
            if (geometry && geometry->getRuntimeModelId() == receiverModelId) {
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
    freeBuffer(resources.voronoi.occupancyPointBuffer, resources.voronoi.occupancyPointBufferOffset);
    resources.voronoi.occupancyPointCount = 0;
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
