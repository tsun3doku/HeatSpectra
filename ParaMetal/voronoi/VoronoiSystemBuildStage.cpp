#include "VoronoiSystemBuildStage.hpp"

#include "spatial/VoxelGrid.hpp"
#include "spatial/TriangleHashGrid.hpp"
#include "voronoi/VoronoiDomainRuntime.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"
#include "voronoi/VoronoiRuntime.hpp"
#include "voronoi/cuda/KNNNeighbors.hpp"
#include "voronoi/cuda/RVD.hpp"

#include "renderers/PointRenderer.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

VoronoiSystemBuildStage::VoronoiSystemBuildStage(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), commandPool(commandPool) {
    rvd = std::make_unique<voronoi::RVD>();
    if (!rvd->initialize(vulkanDevice)) rvd.reset();

    knn = std::make_unique<voronoi::KNNNeighbors>();
    if (!knn->initialize(vulkanDevice)) knn.reset();
}

VoronoiSystemBuildStage::~VoronoiSystemBuildStage() = default;

bool VoronoiSystemBuildStage::prepareDomainGeometry(
    VoronoiRuntime& runtime, float cellSize, int voxelResolution) const {
    if (!(cellSize > 0.0f) || !std::isfinite(cellSize) || voxelResolution < 2) return false;
    VoronoiDomainRuntime* domainRuntime = runtime.getDomainRuntime();
    if (!domainRuntime || runtime.getSeedPositions().empty()) return false;

    runtime.getSeedFlags().assign(runtime.getSeedPositions().size(), 0u);

    if (domainRuntime->isPointDomain()) {
        runtime.getMeshTriangles().clear();
        runtime.setVoxelGridBuilt(false);
        if (rvd) rvd->clearMeshGeometry();
        return true;
    }

    auto* modelRuntime = static_cast<VoronoiModelRuntime*>(domainRuntime);
    if (modelRuntime->getRuntimeModelId() == 0) return false;
    const auto& positions = modelRuntime->getGeometryPositions();
    const auto& indices = modelRuntime->getGeometryTriangleIndices();
    if (positions.empty() || indices.empty() || !rvd) return false;
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(-std::numeric_limits<float>::max());
    for (const glm::vec3& position : positions) {
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    const glm::vec3 padding(3.0f * cellSize);
    const glm::vec3 gridMin = minimum - padding;
    const glm::vec3 unadjustedGridMax = maximum + padding;
    const glm::vec3 gridSize = unadjustedGridMax - gridMin;
    const glm::ivec3 gridDim(
        std::max(2, int(std::ceil(gridSize.x / cellSize))),
        std::max(2, int(std::ceil(gridSize.y / cellSize))),
        std::max(2, int(std::ceil(gridSize.z / cellSize))));
    const glm::vec3 gridMax = gridMin + glm::vec3(gridDim) * cellSize;

    TriangleHashGrid triangleGrid;
    triangleGrid.build(positions, indices, gridMin, gridMax, cellSize);
    runtime.getVoxelGrid().build(positions, indices, triangleGrid, voxelResolution);
    runtime.setVoxelGridBuilt(true);
    extractMeshTriangles(positions, indices, runtime.getMeshTriangles());
    if (!rvd->prepareMeshGeometry(
            runtime.getMeshTriangles(), runtime.getVoxelGrid(), runtime.getSeedPositions(),
            gridMin, gridDim, cellSize, runtime.getSeedFlags())) {
        std::cerr << "[VoronoiBuild] CUDA SDF preparation failed" << std::endl;
        return false;
    }
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

bool VoronoiSystemBuildStage::uploadCandidateInputs(
    const std::vector<glm::vec4>& positions,
    const std::vector<uint32_t>& neighborIndices) {
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

    candidateNeighborIndicesBuffer = newNeighborBuffer;
    candidateNeighborIndicesBufferOffset = newNeighborOffset;
    seedPositionBuffer = newSeedBuffer;
    seedPositionBufferOffset = newSeedOffset;
    return true;
}

bool VoronoiSystemBuildStage::uploadCandidateNodes(
    const std::vector<voronoi::Node>& nodes) {
    if (nodes.empty()) return false;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceSize newOffset = 0;
    if (uploadDeviceBuffer(memoryAllocator, commandPool, nodes.data(),
                           nodes.size() * sizeof(voronoi::Node),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           alignment, newBuffer, newOffset) != VK_SUCCESS) return false;
    candidateNodeBuffer = newBuffer;
    candidateNodeBufferOffset = newOffset;
    return true;
}

bool VoronoiSystemBuildStage::buildPointTopology(
    VoronoiRuntime& runtime,
    const std::vector<uint32_t>& neighborIndices,
    uint32_t neighborCount,
    std::vector<voronoi::Node>& nodes,
    std::vector<voronoi::NodeCoupling>& couplings,
    std::vector<float>& surfacePatchAreas) {
    const auto& positions = runtime.getSeedPositions();
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

bool VoronoiSystemBuildStage::buildMeshTopology(
    VoronoiRuntime& runtime,
    voronoi::KNNNeighbors& neighbors,
    std::vector<voronoi::Node>& nodes,
    std::vector<voronoi::NodeCoupling>& couplings,
    std::vector<float>& surfacePatchAreas) {
    if (!rvd) return false;
    const auto& positions = runtime.getSeedPositions();
    voronoi::RVDInput input{};
    input.seeds = &positions;
    input.seedFlags = &runtime.getSeedFlags();
    input.candidateNeighbors = &neighbors;
    input.domainCorners = &runtime.getPointDomainCorners();
    input.nominalCellSize = runtime.getCellSize();
    voronoi::RVDResult result;
    voronoi::RVDConfig config{};
    config.restrictedChunkSize = 2048;
    if (!rvd->build(input, config, result)) return false;

    runtime.getSeedFlags().swap(result.nodeFlags);
    nodes.swap(result.nodes);
    surfacePatchAreas.swap(result.surfacePatchAreas);
    couplings.clear();
    const size_t interfaceCount = static_cast<size_t>(candidateNodeCount) * voronoi::RVDMaximumInterfaces;
    couplings.reserve(interfaceCount);
    std::vector<voronoi::NodeCoupling> nodeCouplings;
    nodeCouplings.reserve(voronoi::RVDMaximumInterfaces);
    const auto& flags = runtime.getSeedFlags();

    for (uint32_t nodeId = 0; nodeId < candidateNodeCount; ++nodeId) {
        voronoi::Node& node = nodes[nodeId];
        nodeCouplings.clear();

        if ((flags[nodeId] & voronoi::NodeFlags::Ghost) == 0u) {
            const size_t interfaceOffset = static_cast<size_t>(nodeId) * voronoi::RVDMaximumInterfaces;
            for (uint32_t interfaceIndex = 0; interfaceIndex < node.interfaceNeighborCount; ++interfaceIndex) {
                const uint32_t neighborId = result.interfaceNeighborIds[interfaceOffset + interfaceIndex];
                const float area = result.interfaceAreas[interfaceOffset + interfaceIndex];
                if (!(area > 0.0f) || !std::isfinite(area)) continue;

                const glm::vec3 delta = glm::vec3(positions[neighborId]) - glm::vec3(positions[nodeId]);
                const float distance = glm::length(delta);
                if (!(distance > 0.0f) || !std::isfinite(distance)) continue;

                const float conductance = area / distance;
                if (conductance > 0.0f && std::isfinite(conductance)) {
                    nodeCouplings.push_back({neighborId, conductance});
                }
            }
        }
        node.neighborOffset = static_cast<uint32_t>(couplings.size());
        node.neighborCount = static_cast<uint32_t>(nodeCouplings.size());
        node.interfaceNeighborCount = node.neighborCount;
        couplings.insert(couplings.end(), nodeCouplings.begin(), nodeCouplings.end());
    }
    return !couplings.empty();
}

bool VoronoiSystemBuildStage::buildNodeDomain(VoronoiRuntime& runtime, uint32_t maxNeighbors) {
    VoronoiDomainRuntime* domainRuntime = runtime.getDomainRuntime();
    const auto& positions = runtime.getSeedPositions();
    if (!domainRuntime || !knn || positions.empty() ||
        runtime.getSeedFlags().size() != positions.size()) return false;

    candidateNodeCount = static_cast<uint32_t>(positions.size());
    const bool isPoint = domainRuntime->isPointDomain();
    if (isPoint && candidateNodeCount < 2) return false;

    uint32_t neighborCount = voronoi::RVDNormalCandidateCount;
    if (isPoint) {
        neighborCount = std::min(maxNeighbors, candidateNodeCount - 1u);
    }
    if (neighborCount == 0) return false;

    std::vector<uint32_t> knnRows;
    knnRows.reserve(candidateNodeCount);
    const auto& flags = runtime.getSeedFlags();
    for (uint32_t cell = 0; cell < candidateNodeCount; ++cell) {
        if (isPoint || (flags[cell] & voronoi::NodeFlags::Ghost) == 0u) knnRows.push_back(cell);
    }
    if (knnRows.empty()) return false;
    if (!knn->build(positions, neighborCount, knnRows)) return false;

    std::vector<uint32_t> neighborIndices;
    if (!knn->downloadPrefix(neighborCount, neighborIndices)) return false;

    std::vector<voronoi::Node> candidateNodes;
    std::vector<voronoi::NodeCoupling> candidateCouplings;
    std::vector<float> surfacePatchAreas;

    bool topologyBuilt;
    if (isPoint) {
        topologyBuilt = buildPointTopology(
            runtime, neighborIndices, neighborCount, candidateNodes,
            candidateCouplings, surfacePatchAreas);
    } else {
        topologyBuilt = buildMeshTopology(
            runtime, *knn, candidateNodes, candidateCouplings, surfacePatchAreas);
    }
    if (!topologyBuilt) return false;

    if (!uploadCandidateInputs(positions, neighborIndices)) return false;
    if (!uploadCandidateNodes(candidateNodes)) return false;
    if (!compactAndUploadNodeDomain(runtime, candidateNodes, candidateCouplings, surfacePatchAreas)) return false;

    if (isPoint) return true;
    return rebuildOccupancyPointBuffer(runtime);
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
    nodeBuffer = newNodeBuffer;
    nodeBufferOffset = newNodeOffset;
    couplingBuffer = newCouplingBuffer;
    couplingBufferOffset = newCouplingOffset;
    couplingCount = static_cast<uint32_t>(domain.getCouplings().size());
    return true;
}

bool VoronoiSystemBuildStage::rebuildOccupancyPointBuffer(VoronoiRuntime& runtime) {
    occupancyPointBuffer = VK_NULL_HANDLE;
    occupancyPointBufferOffset = 0;
    occupancyPointCount = 0;
    if (!runtime.isVoxelGridBuilt()) return true;

    const VoxelGrid& grid = runtime.getVoxelGrid();
    const auto& occupancy = grid.getOccupancyData();
    const auto& params = grid.getParams();
    const auto& modelRuntime = static_cast<const VoronoiModelRuntime&>(*runtime.getDomainRuntime());
    const glm::mat4& modelMatrix = modelRuntime.getModelMatrix();

    std::vector<PointRenderer::PointVertex> points;
    points.reserve(occupancy.size() / 4);
    const size_t width = static_cast<size_t>(params.gridDim.x) + 1;
    const size_t height = static_cast<size_t>(params.gridDim.y) + 1;
    for (int z = 0; z <= params.gridDim.z; ++z) {
        for (int y = 0; y <= params.gridDim.y; ++y) {
            for (int x = 0; x <= params.gridDim.x; ++x) {
                const size_t index = (static_cast<size_t>(z) * height + y) * width + x;
                if (occupancy[index] == 0u) continue;

                const glm::vec3 position = glm::vec3(modelMatrix * glm::vec4(grid.getCornerPosition(x, y, z), 1.0f));
                glm::vec3 color(0.2f, 1.0f, 0.2f);
                if (occupancy[index] == 1u) {
                    color = glm::vec3(1.0f, 0.2f, 0.2f);
                }
                points.push_back({position, color});
            }
        }
    }
    if (points.empty()) return true;
    const VkDeviceSize size = points.size() * sizeof(PointRenderer::PointVertex);
    auto [buffer, offset] = memoryAllocator.allocate(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        alignof(PointRenderer::PointVertex));
    if (buffer == VK_NULL_HANDLE) return false;
    void* mapped = memoryAllocator.getMappedPointer(buffer, offset);
    if (!mapped) {
        memoryAllocator.free(buffer, offset);
        return false;
    }
    std::memcpy(mapped, points.data(), static_cast<size_t>(size));
    occupancyPointBuffer = buffer;
    occupancyPointBufferOffset = offset;
    occupancyPointCount = static_cast<uint32_t>(points.size());
    return true;
}

void VoronoiSystemBuildStage::cleanupResources() {
    if (rvd) rvd->cleanup();
    if (knn) knn->cleanup();
}

void VoronoiSystemBuildStage::cleanup() {
    candidateNodeBuffer = VK_NULL_HANDLE;
    candidateNodeBufferOffset = 0;
    candidateNeighborIndicesBuffer = VK_NULL_HANDLE;
    candidateNeighborIndicesBufferOffset = 0;
    seedPositionBuffer = VK_NULL_HANDLE;
    seedPositionBufferOffset = 0;
    occupancyPointBuffer = VK_NULL_HANDLE;
    occupancyPointBufferOffset = 0;
    nodeBuffer = VK_NULL_HANDLE;
    nodeBufferOffset = 0;
    couplingBuffer = VK_NULL_HANDLE;
    couplingBufferOffset = 0;
    candidateNodeCount = 0;
    occupancyPointCount = 0;
    couplingCount = 0;
}
