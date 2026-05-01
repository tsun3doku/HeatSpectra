#include "VoronoiSystemRuntime.hpp"

#include <algorithm>

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiCandidateCompute.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <iostream>
#include <limits>
#include <unordered_set>

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

void VoronoiSystemRuntime::setReceiverGeometry(
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
    const std::vector<VkBufferView>& inputLengthViews) {
    activeReceiverModelIds = receiverModelIds;
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
                receiverSurfaceVertices[index],
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
            std::cerr << "[VoronoiSystemRuntime] Failed to create Voronoi buffers for runtimeModelId="
                      << receiverId << std::endl;
            modelRuntime->cleanup();
            continue;
        }

        if (!modelRuntime->createSurfaceBuffers() || !modelRuntime->initializeSurfaceBuffer()) {
            std::cerr << "[VoronoiSystemRuntime] Failed to create surface buffers for runtimeModelId="
                      << receiverId << std::endl;
            modelRuntime->cleanup();
            continue;
        }

        modelRuntimes.push_back(std::move(modelRuntime));
    }
}

void VoronoiSystemRuntime::clearReceiverGeometry() {
    if (activeReceiverModelIds.empty() && modelRuntimes.empty()) {
        return;
    }

    clearReceiverDomains();
    invalidateMaterialization();
    activeReceiverModelIds.clear();

    for (auto& modelRuntime : modelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    modelRuntimes.clear();
}

void VoronoiSystemRuntime::setParams(float updatedCellSize, int updatedVoxelResolution) {
    if (cellSize == updatedCellSize && voxelResolution == updatedVoxelResolution) {
        return;
    }

    cellSize = updatedCellSize;
    voxelResolution = updatedVoxelResolution;
    clearReceiverDomains();
    invalidateMaterialization();
}

void VoronoiSystemRuntime::clearReceiverDomains() {
    receiverVoronoiDomains.clear();
}

void VoronoiSystemRuntime::markSeederReady() {
    voronoiSeederReady = true;
}

void VoronoiSystemRuntime::markReady() {
    voronoiReady = true;
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

    freeBuffer(resources.voronoiNodeBuffer, resources.voronoiNodeBufferOffset);
    resources.mappedVoronoiNodeData = nullptr;
    freeBuffer(resources.voronoiNeighborBuffer, resources.voronoiNeighborBufferOffset);
    freeBuffer(resources.neighborIndicesBuffer, resources.neighborIndicesBufferOffset);
    freeBuffer(resources.interfaceAreasBuffer, resources.interfaceAreasBufferOffset);
    resources.mappedInterfaceAreasData = nullptr;
    freeBuffer(resources.interfaceNeighborIdsBuffer, resources.interfaceNeighborIdsBufferOffset);
    resources.mappedInterfaceNeighborIdsData = nullptr;
    freeBuffer(resources.gmlsInterfaceBuffer, resources.gmlsInterfaceBufferOffset);
    freeBuffer(resources.meshTriangleBuffer, resources.meshTriangleBufferOffset);
    freeBuffer(resources.seedPositionBuffer, resources.seedPositionBufferOffset);
    resources.mappedSeedPositionData = nullptr;
    freeBuffer(resources.seedFlagsBuffer, resources.seedFlagsBufferOffset);
    resources.mappedSeedFlagsData = nullptr;
    freeBuffer(resources.occupancyPointBuffer, resources.occupancyPointBufferOffset);
    resources.occupancyPointCount = 0;
    freeBuffer(resources.debugCellGeometryBuffer, resources.debugCellGeometryBufferOffset);
    resources.mappedDebugCellGeometryData = nullptr;
    freeBuffer(resources.voronoiDumpBuffer, resources.voronoiDumpBufferOffset);
    resources.mappedVoronoiDumpData = nullptr;
    freeBuffer(resources.voxelGridParamsBuffer, resources.voxelGridParamsBufferOffset);
    freeBuffer(resources.voxelOccupancyBuffer, resources.voxelOccupancyBufferOffset);
    freeBuffer(resources.voxelTrianglesListBuffer, resources.voxelTrianglesListBufferOffset);
    freeBuffer(resources.voxelOffsetsBuffer, resources.voxelOffsetsBufferOffset);
    resources.voronoiNodeCount = 0;

    for (auto& modelRuntime : modelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    modelRuntimes.clear();
}
