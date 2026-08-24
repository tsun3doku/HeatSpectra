#include "VoronoiSystemBuildStage.hpp"

#include "spatial/VoxelGrid.hpp"
#include "spatial/TriangleHashGrid.hpp"
#include "heat/GlobalContactLevelSet.hpp"
#include "voronoi/VoronoiDomainRuntime.hpp"
#include "voronoi/VoronoiRuntime.hpp"
#include "voronoi/cuda/KNNNeighbors.hpp"
#include "voronoi/cuda/RVD.hpp"
#include "voronoi/cuda/GlobalCutCell.cuh"
#include "voronoi/cuda/GlobalSdf.cuh"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <glm/gtc/constants.hpp>

VoronoiSystemBuildStage::VoronoiSystemBuildStage(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), commandPool(commandPool) {
    knn = std::make_unique<voronoi::KNNNeighbors>();
    if (!knn->initialize(vulkanDevice)) knn.reset();

    globalCutCell = std::make_unique<voronoi::GlobalCutCell>();
    if (!globalCutCell->initialize(vulkanDevice)) globalCutCell.reset();

    contactLevelSet = std::make_unique<heat::GlobalContactLevelSet>(vulkanDevice, memoryAllocator, commandPool);
    candidate = std::make_unique<voronoi::VoronoiCandidate>(vulkanDevice, memoryAllocator, commandPool);
}

VoronoiSystemBuildStage::~VoronoiSystemBuildStage() = default;

bool VoronoiSystemBuildStage::buildNodeDomain(VoronoiRuntime& runtime, uint32_t maxNeighbors) {
    VoronoiDomainRuntime* domainRuntime = runtime.getDomainRuntime();
    const auto& positions = runtime.getSeedPositions();
    if (!domainRuntime || !knn || positions.empty() ||
        runtime.getSeedFlags().size() != positions.size()) return false;

    candidateNodeCount = static_cast<uint32_t>(positions.size());
    if (candidateNodeCount < 2) return false;

    uint32_t neighborCount = std::min(maxNeighbors, candidateNodeCount - 1u);
    if (neighborCount == 0) return false;

    std::vector<uint32_t> knnRows;
    knnRows.reserve(candidateNodeCount);
    for (uint32_t cell = 0; cell < candidateNodeCount; ++cell) {
        knnRows.push_back(cell);
    }
    if (knnRows.empty()) return false;
    if (!knn->build(positions, neighborCount, knnRows)) return false;

    std::vector<uint32_t> neighborIndices;
    if (!knn->downloadPrefix(neighborCount, neighborIndices)) return false;

    std::vector<voronoi::Node> candidateNodes;
    std::vector<voronoi::NodeCoupling> candidateCouplings;
    std::vector<float> surfacePatchAreas;

    if (!buildPointTopology(
            positions, neighborIndices, neighborCount, candidateNodes,
            candidateCouplings, surfacePatchAreas)) return false;

    if (!uploadCandidateInputs(positions, neighborIndices)) return false;
    if (!uploadCandidateNodes(candidateNodes)) return false;
    return compactAndUploadNodeDomain(runtime, candidateNodes, candidateCouplings, surfacePatchAreas);
}

bool VoronoiSystemBuildStage::buildGlobalDomain(VoronoiRuntime& runtime) {
    const auto& runtimeModelIds = runtime.getGlobalRemeshRuntimeModelIds();
    const auto& positions = runtime.getGlobalRemeshPositions();
    const auto& triangleIndices = runtime.getGlobalRemeshTriangleIndices();
    const std::vector<glm::vec4>& seeds = runtime.getSeedPositions();
    const std::array<glm::vec3, 8>& corners = runtime.getPointDomainCorners();
    if (!globalCutCell || runtimeModelIds.empty() ||
        seeds.size() < voronoi::RVDNormalCandidateCount + 1u) {
        if (runtimeModelIds.empty()) {
            std::cerr << "[VoronoiBuild] Global domain requires at least one remesh instance"
                      << std::endl;
        } else {
            std::cerr << "[VoronoiBuild] Global domain requires at least "
                      << voronoi::RVDNormalCandidateCount + 1u << " seeds" << std::endl;
        }
        return false;
    }

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(-std::numeric_limits<float>::max());
    for (const glm::vec3& corner : corners) {
        boundsMin = glm::min(boundsMin, corner);
        boundsMax = glm::max(boundsMax, corner);
    }
    const glm::vec3 extent = boundsMax - boundsMin;
    const float longestExtent = std::max(std::max(extent.x, extent.y), extent.z);
    if (!(longestExtent > 0.0f) || !std::isfinite(longestExtent)) return false;
    const int resolution = runtime.getVoxelResolution();
    if (resolution < 2) return false;
    const float spacing = longestExtent / static_cast<float>(resolution - 1);
    if (!(spacing > 0.0f) || !std::isfinite(spacing)) return false;
    const float padding = runtime.getGlobalSdfPadding() + spacing;
    const glm::vec3 gridMin = boundsMin - glm::vec3(padding);
    const glm::vec3 gridSize = extent + glm::vec3(2.0f * padding);
    const glm::ivec3 gridDim(
        std::max(2, int(std::ceil(gridSize.x / spacing))),
        std::max(2, int(std::ceil(gridSize.y / spacing))),
        std::max(2, int(std::ceil(gridSize.z / spacing))));
    const glm::vec3 gridMax = gridMin + glm::vec3(gridDim) * spacing;

    runtime.getSeedFlags().assign(seeds.size(), 0u);

    std::vector<VoxelGrid> instanceGrids;
    std::vector<std::vector<std::array<glm::vec4, 3>>> instanceTriangles;
    instanceGrids.reserve(runtimeModelIds.size());
    instanceTriangles.reserve(runtimeModelIds.size());
    std::vector<voronoi::GlobalCutCell::InstanceInput> instanceInputs;
    instanceInputs.reserve(runtimeModelIds.size());
    std::vector<voronoi::globalsdf::Instance> sdfInstances;
    sdfInstances.reserve(runtimeModelIds.size());

    for (size_t i = 0; i < runtimeModelIds.size(); ++i) {
        TriangleHashGrid triangleGrid;
        triangleGrid.build(positions[i], triangleIndices[i], gridMin, gridMax, spacing);
        VoxelGrid instanceGrid;
        instanceGrid.build(positions[i], triangleIndices[i], triangleGrid, resolution);
        std::vector<std::array<glm::vec4, 3>> triangles;
        extractMeshTriangles(positions[i], triangleIndices[i], triangles);
        instanceGrids.push_back(std::move(instanceGrid));
        instanceTriangles.push_back(std::move(triangles));
        instanceInputs.push_back({runtimeModelIds[i], &instanceGrids.back(), &instanceTriangles.back()});
        sdfInstances.push_back({instanceInputs.back().voxelGrid, instanceInputs.back().triangles});
    }
    voronoi::globalsdf::BuildInput sdfInput{};
    sdfInput.instances = &sdfInstances;
    sdfInput.gridMin = gridMin;
    sdfInput.gridDim = gridDim;
    sdfInput.cellSize = spacing;
    voronoi::globalsdf::BuildResult sdfResult;
    if (!voronoi::globalsdf::build(sdfInput, sdfResult, 0)) {
        std::cerr << "[VoronoiBuild] GlobalSdf build failed" << std::endl;
        return false;
    }

    voronoi::GlobalCutCell::BuildInput input{};
    input.instances = &instanceInputs;
    input.seeds = &seeds;
    input.domainCorners = &corners;
    input.sdfGridMin = gridMin;
    input.sdfGridDim = gridDim;
    input.sdfSpacing = spacing;
    input.nominalCellSize = runtime.getCellSize();
    input.maxNeighbors = voronoi::RVDNormalCandidateCount;
    voronoi::GlobalCutCell::BuildResult result;
    if (!globalCutCell->build(input, result)) {
        std::cerr << "[VoronoiBuild] GlobalCutCell build failed" << std::endl;
        return false;
    }

    globalFragmentInstanceIds = std::move(result.fragmentInstanceIds);
    globalFragmentSeedIds = std::move(result.fragmentSeedIds);
    globalSurfaceBoundaryAreas = std::move(result.surfaceBoundaryAreas);
    globalFragmentVolumes = std::move(result.fragmentVolumes);
    globalFaceInstanceIds = std::move(result.faceInstanceIds);
    globalFaceFragmentA = std::move(result.faceFragmentA);
    globalFaceFragmentB = std::move(result.faceFragmentB);
    globalFaceAreas = std::move(result.faceAreas);
    globalCutFaceFragmentA = result.cutFaceFragmentA;
    globalCutFaceFragmentB = result.cutFaceFragmentB;
    globalCutFaceAreas = result.cutFaceAreas;
    globalCutFaceGaps = result.cutFaceGaps;
    globalInstanceFragmentCounts = std::move(result.instanceFragmentCounts);
    globalSdfValues = sdfResult.values;
    globalSdfRuntimeModelIds = runtimeModelIds;
    globalSdfGridMin = gridMin;
    globalSdfGridDim = gridDim;
    globalSdfCellSize = spacing;
    globalSeedPositions = seeds;
    globalDomainCorners = corners;

    const uint32_t channelCount = static_cast<uint32_t>(runtimeModelIds.size());
    if (!contactLevelSet || !contactLevelSet->build(sdfResult, result, channelCount, gridMin, gridDim, spacing)) {
        std::cerr << "[VoronoiBuild] GlobalContactLevelSet build failed." << std::endl;
        cleanupResources();
        return false;
    }

    return true;
}

bool VoronoiSystemBuildStage::buildGlobalDisplayCandidates(VoronoiRuntime& runtime) {
    const auto& seeds = runtime.getSeedPositions();
    const auto& runtimeModelIds = runtime.getGlobalRemeshRuntimeModelIds();
    const auto& surfacePositions = runtime.getGlobalRemeshSurfacePositions();
    const auto& surfaceTriangleIndices = runtime.getGlobalRemeshSurfaceTriangleIndices();
    if (!knn || seeds.size() < 2 || runtimeModelIds.empty()) return false;

    candidateNodeCount = static_cast<uint32_t>(seeds.size());
    const uint32_t neighborCount = std::min(uint32_t{50}, candidateNodeCount - 1u);
    if (neighborCount == 0) return false;

    std::vector<uint32_t> knnRows;
    knnRows.reserve(candidateNodeCount);
    for (uint32_t cell = 0; cell < candidateNodeCount; ++cell) {
        knnRows.push_back(cell);
    }
    if (!knn->build(seeds, neighborCount, knnRows)) return false;

    std::vector<uint32_t> neighborIndices;
    if (!knn->downloadPrefix(neighborCount, neighborIndices)) return false;

    std::vector<voronoi::Node> candidateNodes;
    std::vector<voronoi::NodeCoupling> candidateCouplings;
    std::vector<float> surfacePatchAreas;
    if (!buildPointTopology(
            seeds, neighborIndices, neighborCount, candidateNodes,
            candidateCouplings, surfacePatchAreas)) return false;
    if (!uploadCandidateInputs(seeds, neighborIndices)) return false;
    if (!uploadCandidateNodes(candidateNodes)) return false;

    if (!candidate || !candidate->build(runtimeModelIds, surfacePositions, surfaceTriangleIndices)) {
        return false;
    }
    return candidate->getModelCount() > 0;
}


const std::vector<VkImageView>& VoronoiSystemBuildStage::getGlobalSdfImageViews() const {
    static const std::vector<VkImageView> empty;
    return contactLevelSet ? contactLevelSet->getSdfImageViews() : empty;
}

const std::vector<VkImageView>& VoronoiSystemBuildStage::getGlobalPsiImageViews() const {
    static const std::vector<VkImageView> empty;
    return contactLevelSet ? contactLevelSet->getPsiImageViews() : empty;
}

VkSampler VoronoiSystemBuildStage::getGlobalSdfSampler() const {
    return contactLevelSet ? contactLevelSet->getSampler() : VK_NULL_HANDLE;
}

VkBuffer VoronoiSystemBuildStage::getContactRegionBuffer() const {
    return contactLevelSet ? contactLevelSet->getContactRegionBuffer() : VK_NULL_HANDLE;
}

VkDeviceSize VoronoiSystemBuildStage::getContactRegionBufferOffset() const {
    return contactLevelSet ? contactLevelSet->getContactRegionBufferOffset() : 0;
}

VkDeviceSize VoronoiSystemBuildStage::getContactRegionBufferSize() const {
    return contactLevelSet ? contactLevelSet->getContactRegionBufferSize() : 0;
}

VkBuffer VoronoiSystemBuildStage::getIndirectDrawBuffer() const {
    return contactLevelSet ? contactLevelSet->getIndirectDrawBuffer() : VK_NULL_HANDLE;
}

VkDeviceSize VoronoiSystemBuildStage::getIndirectDrawBufferOffset() const {
    return contactLevelSet ? contactLevelSet->getIndirectDrawBufferOffset() : 0;
}

bool VoronoiSystemBuildStage::buildPointTopology(
    const std::vector<glm::vec4>& positions,
    const std::vector<uint32_t>& neighborIndices,
    uint32_t neighborCount,
    std::vector<voronoi::Node>& nodes,
    std::vector<voronoi::NodeCoupling>& couplings,
    std::vector<float>& surfacePatchAreas) {
    const size_t nodeCount = positions.size();

    nodes.assign(nodeCount, {});
    surfacePatchAreas.assign(nodeCount, 0.0f);
    couplings.clear();
    couplings.reserve(neighborIndices.size());
    std::vector<voronoi::NodeCoupling> nodeCouplings;
    nodeCouplings.reserve(neighborCount);

    for (uint32_t nodeId = 0; nodeId < static_cast<uint32_t>(nodeCount); ++nodeId) {
        voronoi::Node& node = nodes[nodeId];
        node.volume = 1.0f;
        nodeCouplings.clear();

        const glm::vec3 nodePosition(positions[nodeId]);
        const uint32_t* neighbors = neighborIndices.data() + static_cast<size_t>(nodeId) * neighborCount;
        for (uint32_t candidate = 0; candidate < neighborCount; ++candidate) {
            const uint32_t neighborId = neighbors[candidate];
            const float distance = glm::length(glm::vec3(positions[neighborId]) - nodePosition);
            if (!(distance > 0.0f) || !std::isfinite(distance)) continue;
            nodeCouplings.push_back({neighborId, 1.0f / distance});
        }
        node.neighborOffset = static_cast<uint32_t>(couplings.size());
        node.neighborCount = static_cast<uint32_t>(nodeCouplings.size());
        node.interfaceNeighborCount = node.neighborCount;
        couplings.insert(couplings.end(), nodeCouplings.begin(), nodeCouplings.end());
    }
    return !couplings.empty();
}

bool VoronoiSystemBuildStage::compactAndUploadNodeDomain(
    VoronoiRuntime& runtime,
    const std::vector<voronoi::Node>& candidateNodes,
    const std::vector<voronoi::NodeCoupling>& candidateCouplings,
    const std::vector<float>& surfacePatchAreas) {
    runtime.getNodeDomain().rebuild(runtime.getSeedFlags(), runtime.getSeedPositions(), candidateNodes,
                                    candidateCouplings, surfacePatchAreas);
    const VoronoiNodeDomain& domain = runtime.getNodeDomain();
    if (domain.getNodeCount() == 0) return false;
    return uploadNodeDomainBuffers(domain);
}

bool VoronoiSystemBuildStage::uploadNodeDomainBuffers(const VoronoiNodeDomain& domain) {
    if (domain.getNodes().empty() || domain.getCouplings().empty()) return false;
    constexpr VkDeviceSize alignment = 16;
    VkBuffer newNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize newNodeOffset = 0;
    if (uploadDeviceBuffer(memoryAllocator, commandPool, domain.getNodes().data(),
                           domain.getNodes().size() * sizeof(voronoi::Node),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           alignment, newNodeBuffer, newNodeOffset) != VK_SUCCESS) return false;
    VkBuffer newCouplingBuffer = VK_NULL_HANDLE;
    VkDeviceSize newCouplingOffset = 0;
    if (uploadDeviceBuffer(memoryAllocator, commandPool, domain.getCouplings().data(),
                           domain.getCouplings().size() * sizeof(voronoi::NodeCoupling),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           alignment, newCouplingBuffer, newCouplingOffset) != VK_SUCCESS) {
        memoryAllocator.free(newNodeBuffer, newNodeOffset);
        return false;
    }
    if (nodeBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, nodeBuffer, nodeBufferOffset);
    }
    if (couplingBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, couplingBuffer, couplingBufferOffset);
    }

    nodeBuffer = newNodeBuffer;
    nodeBufferOffset = newNodeOffset;
    couplingBuffer = newCouplingBuffer;
    couplingBufferOffset = newCouplingOffset;
    couplingCount = static_cast<uint32_t>(domain.getCouplings().size());
    return true;
}

bool VoronoiSystemBuildStage::uploadCandidateInputs(const std::vector<glm::vec4>& positions, const std::vector<uint32_t>& neighborIndices) {
    if (positions.empty() || neighborIndices.empty()) return false;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    VkBuffer newNeighborBuffer = VK_NULL_HANDLE;
    VkDeviceSize newNeighborOffset = 0;
    if (uploadDeviceBuffer(memoryAllocator, commandPool, neighborIndices.data(),
                           neighborIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           alignment, newNeighborBuffer, newNeighborOffset) != VK_SUCCESS) return false;

    VkBuffer newSeedBuffer = VK_NULL_HANDLE;
    VkDeviceSize newSeedOffset = 0;
    if (uploadDeviceBuffer(memoryAllocator, commandPool, positions.data(),
                           positions.size() * sizeof(glm::vec4),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           alignment, newSeedBuffer, newSeedOffset) != VK_SUCCESS) {
        memoryAllocator.free(newNeighborBuffer, newNeighborOffset);
        return false;
    }

    if (candidateNeighborIndicesBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, candidateNeighborIndicesBuffer, candidateNeighborIndicesBufferOffset);
    }
    if (seedPositionBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, seedPositionBuffer, seedPositionBufferOffset);
    }

    candidateNeighborIndicesBuffer = newNeighborBuffer;
    candidateNeighborIndicesBufferOffset = newNeighborOffset;
    seedPositionBuffer = newSeedBuffer;
    seedPositionBufferOffset = newSeedOffset;
    return true;
}

bool VoronoiSystemBuildStage::uploadCandidateNodes(const std::vector<voronoi::Node>& nodes) {
    if (nodes.empty()) return false;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceSize newOffset = 0;
    if (uploadDeviceBuffer(memoryAllocator, commandPool, nodes.data(),
                           nodes.size() * sizeof(voronoi::Node),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           alignment, newBuffer, newOffset) != VK_SUCCESS) return false;

    if (candidateNodeBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, candidateNodeBuffer, candidateNodeBufferOffset);
    }

    candidateNodeBuffer = newBuffer;
    candidateNodeBufferOffset = newOffset;
    return true;
}

void VoronoiSystemBuildStage::extractMeshTriangles(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices,
    std::vector<std::array<glm::vec4, 3>>& outMeshTriangles) const {
    outMeshTriangles.clear();
    outMeshTriangles.reserve(indices.size() / 3);
    for (size_t index = 0; index + 2 < indices.size(); index += 3) {
        const uint32_t first = indices[index];
        const uint32_t second = indices[index + 1];
        const uint32_t third = indices[index + 2];
        outMeshTriangles.push_back({glm::vec4(positions[first], 0.0f),
                                    glm::vec4(positions[second], 0.0f),
                                    glm::vec4(positions[third], 0.0f)});
    }
}

void VoronoiSystemBuildStage::cleanupResources() {
    if (knn) knn->cleanup();
    if (globalCutCell) globalCutCell->cleanup();
    if (contactLevelSet) contactLevelSet->cleanup();

    if (candidateNeighborIndicesBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, candidateNeighborIndicesBuffer, candidateNeighborIndicesBufferOffset);
        candidateNeighborIndicesBuffer = VK_NULL_HANDLE;
        candidateNeighborIndicesBufferOffset = 0;
    }
    if (seedPositionBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, seedPositionBuffer, seedPositionBufferOffset);
        seedPositionBuffer = VK_NULL_HANDLE;
        seedPositionBufferOffset = 0;
    }
    if (candidateNodeBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, candidateNodeBuffer, candidateNodeBufferOffset);
        candidateNodeBuffer = VK_NULL_HANDLE;
        candidateNodeBufferOffset = 0;
    }
    if (nodeBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, nodeBuffer, nodeBufferOffset);
        nodeBuffer = VK_NULL_HANDLE;
        nodeBufferOffset = 0;
    }
    if (couplingBuffer != VK_NULL_HANDLE) {
        freeBuffer(memoryAllocator, couplingBuffer, couplingBufferOffset);
        couplingBuffer = VK_NULL_HANDLE;
        couplingBufferOffset = 0;
    }

    if (candidate) {
        candidate->cleanup();
    }
}

void VoronoiSystemBuildStage::cleanup() {
    cleanupResources();

    candidateNodeCount = 0;
    couplingCount = 0;
    globalFragmentInstanceIds.clear();
    globalFragmentSeedIds.clear();
    globalSurfaceBoundaryAreas.clear();
    globalFragmentVolumes.clear();
    globalFaceInstanceIds.clear();
    globalFaceFragmentA.clear();
    globalFaceFragmentB.clear();
    globalFaceAreas.clear();
    globalCutFaceFragmentA.clear();
    globalCutFaceFragmentB.clear();
    globalCutFaceAreas.clear();
    globalCutFaceGaps.clear();
    globalInstanceFragmentCounts.clear();
    globalSdfValues.clear();
    globalSdfRuntimeModelIds.clear();
    globalSdfGridMin = {};
    globalSdfGridDim = {};
    globalSdfCellSize = 0.0f;
    globalSeedPositions.clear();
    globalDomainCorners = {};
}