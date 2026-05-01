#include "VoronoiBuilder.hpp"

#include "spatial/VoxelGrid.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include "renderers/PointRenderer.hpp"
#include "util/GMLS.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiGeoCompute.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <libs/nanoflann/include/nanoflann.hpp>

namespace {

struct SeedPointCloudAdapter {
    const std::vector<glm::vec3>& pts;
    inline size_t kdtree_get_point_count() const { return pts.size(); }
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
        return pts[idx][static_cast<int>(dim)];
    }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, SeedPointCloudAdapter>,
    SeedPointCloudAdapter,
    3>;

} // namespace

VoronoiBuilder::VoronoiBuilder(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    VoronoiResources& resources)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resources(resources) {
}

bool VoronoiBuilder::tryCreateStorageBuffer(
    const char* label,
    const void* data,
    VkDeviceSize size,
    VkBuffer& buffer,
    VkDeviceSize& offset,
    void** mapped,
    bool hostVisible) const {
    const VkResult result = createStorageBuffer(
        memoryAllocator,
        vulkanDevice,
        data,
        size,
        buffer,
        offset,
        mapped,
        hostVisible);

    if (result != VK_SUCCESS || buffer == VK_NULL_HANDLE) {
        std::cerr << "[VoronoiBuilder] Failed to create " << label
                  << " storage buffer (VkResult=" << result << ")" << std::endl;
        return false;
    }

    return true;
}

bool VoronoiBuilder::buildDomains(
    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes,
    std::vector<VoronoiDomain>& receiverVoronoiDomains,
    float cellSize,
    int voxelResolution,
    uint32_t maxNeighbors) const {
    receiverVoronoiDomains.clear();

    std::unordered_set<uint32_t> seenReceiverModelIds;
    for (const auto& modelRuntimePtr : modelRuntimes) {
        VoronoiModelRuntime* modelRuntime = modelRuntimePtr.get();
        if (!modelRuntime) {
            continue;
        }

        const uint32_t receiverModelId = modelRuntime->getRuntimeModelId();
        if (receiverModelId == 0 || !seenReceiverModelIds.insert(receiverModelId).second) {
            continue;
        }

        VoronoiDomain domain{};
        domain.receiverModelId = receiverModelId;
        domain.modelRuntime = modelRuntime;
        domain.seeder = std::make_unique<VoronoiSeeder>();
        domain.integrator = std::make_unique<VoronoiIntegrator>();
        if (!domain.seeder || !domain.integrator) {
            std::cerr << "[VoronoiBuilder] Skipping receiver " << receiverModelId
                      << ": failed to allocate Voronoi domain helpers" << std::endl;
            continue;
        }

        const SupportingHalfedge::IntrinsicMesh& intrinsicMesh = modelRuntime->getIntrinsicMesh();
        const std::vector<glm::vec3>& geometryPositions = modelRuntime->getGeometryPositions();
        const std::vector<uint32_t>& geometryIndices = modelRuntime->getGeometryTriangleIndices();
        domain.seeder->generateSeeds(
            intrinsicMesh,
            geometryPositions,
            geometryIndices,
            cellSize,
            domain.voxelGrid,
            voxelResolution);
        domain.voxelGridBuilt = (domain.voxelGrid.getGridSize() > 0);

        const std::vector<VoronoiSeeder::Seed>& domainSeeds = domain.seeder->getSeeds();
        if (domainSeeds.empty()) {
            std::cerr << "[VoronoiBuilder] Skipping receiver " << receiverModelId
                      << ": Voronoi seeding produced zero seeds" << std::endl;
            continue;
        }

        std::vector<glm::dvec3> seedPositions;
        seedPositions.reserve(domainSeeds.size());
        domain.seedFlags.clear();
        domain.seedFlags.reserve(domainSeeds.size());
        for (const VoronoiSeeder::Seed& seed : domainSeeds) {
            seedPositions.push_back(glm::dvec3(seed.pos));
            uint32_t flags = 0u;
            if (seed.isSurface) {
                flags |= 2u;
            }
            domain.seedFlags.push_back(flags);
        }

        domain.integrator->computeNeighbors(seedPositions, maxNeighbors);
        domain.integrator->extractMeshTriangles(geometryPositions, geometryIndices);
        domain.nodeCount = static_cast<uint32_t>(domain.seedFlags.size());
        receiverVoronoiDomains.push_back(std::move(domain));
    }

    if (receiverVoronoiDomains.empty()) {
        std::cerr << "[VoronoiBuilder] No valid receiver Voronoi domains were created." << std::endl;
        return false;
    }

    uint32_t runningOffset = 0;
    for (VoronoiDomain& domain : receiverVoronoiDomains) {
        domain.nodeOffset = runningOffset;
        runningOffset += domain.nodeCount;
    }

    return true;
}

void VoronoiBuilder::setGhost(std::vector<VoronoiDomain>& receiverVoronoiDomains, bool fromVolumes) {
    if (fromVolumes) {
        if (resources.voronoiNodeCount == 0 || !resources.mappedVoronoiNodeData || !resources.mappedSeedFlagsData) {
            return;
        }

        const voronoi::Node* nodes = static_cast<const voronoi::Node*>(resources.mappedVoronoiNodeData);
        uint32_t* seedFlags = static_cast<uint32_t*>(resources.mappedSeedFlagsData);

        uint32_t promotedCount = 0;
        for (uint32_t i = 0; i < resources.voronoiNodeCount; ++i) {
            if ((seedFlags[i] & 1u) != 0u) {
                continue;
            }
            if (std::abs(nodes[i].volume) <= 1e-12f) {
                seedFlags[i] |= 1u;
                ++promotedCount;
            }
        }

        const uint32_t* globalFlags = static_cast<const uint32_t*>(resources.mappedSeedFlagsData);
        for (VoronoiDomain& domain : receiverVoronoiDomains) {
            if (domain.seedFlags.size() != domain.nodeCount) {
                continue;
            }
            std::copy(globalFlags + domain.nodeOffset,
                      globalFlags + domain.nodeOffset + domain.nodeCount,
                      domain.seedFlags.data());
        }
    } else {
        for (VoronoiDomain& domain : receiverVoronoiDomains) {
            if (!domain.voxelGridBuilt || !domain.seeder) {
                continue;
            }
            const auto& seedPositions = domain.integrator->getSeedPositions();
            if (seedPositions.size() != domain.seedFlags.size()) {
                continue;
            }
            const float maxDistFromSurface = domain.seeder->getCellSize() * 1.5f;
            uint32_t ghostCount = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(seedPositions.size()); ++i) {
                const glm::vec4& seed = seedPositions[i];
                glm::ivec3 voxel = domain.voxelGrid.worldToVoxel(glm::vec3(seed));
                uint8_t occ = domain.voxelGrid.getOccupancy(voxel.x, voxel.y, voxel.z);
                bool isInside = (occ == 2 || occ == 1);
                float distToSurface = domain.seeder->sampleSDFGrid(glm::vec3(seed));
                if (!isInside && distToSurface > maxDistFromSurface) {
                    domain.seedFlags[i] |= 1u;
                    ++ghostCount;
                }
            }
        }
    }
}

bool VoronoiBuilder::createVoronoiGeometryBuffers(
    const std::vector<voronoi::Node>& initialNodes,
    const std::vector<glm::vec4>& seedPositions,
    const std::vector<uint32_t>& seedFlags,
    const std::vector<uint32_t>& neighborIndices,
    bool debugEnable,
    uint32_t maxNeighbors) {
    if (seedPositions.empty() || seedFlags.empty() || initialNodes.empty()) {
        std::cerr << "[VoronoiBuilder] Failed to create Voronoi buffers: empty node/seed data" << std::endl;
        return false;
    }

    if (seedPositions.size() != seedFlags.size() || seedPositions.size() != initialNodes.size()) {
        std::cerr << "[VoronoiBuilder] Failed to create Voronoi buffers: node/seed/flag count mismatch" << std::endl;
        return false;
    }

    resources.voronoiNodeCount = static_cast<uint32_t>(seedPositions.size());
    void* mappedPtr = nullptr;

    VkDeviceSize bufferSize = sizeof(voronoi::Node) * resources.voronoiNodeCount;
    if (!tryCreateStorageBuffer(
            "voronoi node",
            initialNodes.data(),
            bufferSize,
            resources.voronoiNodeBuffer,
            resources.voronoiNodeBufferOffset,
            &resources.mappedVoronoiNodeData)) {
        return false;
    }

    std::vector<uint32_t> flattenedNeighbors = neighborIndices;
    if (flattenedNeighbors.empty()) {
        flattenedNeighbors.resize(
            static_cast<size_t>(resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors),
            UINT32_MAX);
    }

    bufferSize = sizeof(uint32_t) * flattenedNeighbors.size();
    if (!tryCreateStorageBuffer(
            "neighbor indices",
            flattenedNeighbors.data(),
            bufferSize,
            resources.neighborIndicesBuffer,
            resources.neighborIndicesBufferOffset,
            &mappedPtr)) {
        return false;
    }

    const size_t interfaceDataSize =
        static_cast<size_t>(resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors);
    std::vector<float> emptyAreas(interfaceDataSize, 0.0f);
    bufferSize = sizeof(float) * interfaceDataSize;

    void* mappedInterfaceAreas = nullptr;
    if (!tryCreateStorageBuffer(
            "interface areas",
            emptyAreas.data(),
            bufferSize,
            resources.interfaceAreasBuffer,
            resources.interfaceAreasBufferOffset,
            &mappedInterfaceAreas)) {
        return false;
    }

    std::vector<uint32_t> emptyIds(interfaceDataSize, UINT32_MAX);
    bufferSize = sizeof(uint32_t) * interfaceDataSize;

    void* mappedInterfaceNeighborIds = nullptr;
    if (!tryCreateStorageBuffer(
            "interface neighbor ids",
            emptyIds.data(),
            bufferSize,
            resources.interfaceNeighborIdsBuffer,
            resources.interfaceNeighborIdsBufferOffset,
            &mappedInterfaceNeighborIds)) {
        return false;
    }

    resources.mappedInterfaceAreasData = mappedInterfaceAreas;
    resources.mappedInterfaceNeighborIdsData = mappedInterfaceNeighborIds;

    uint32_t numDebugCells = debugEnable ? resources.voronoiNodeCount : 1u;
    std::vector<voronoi::DebugCellGeometry> debugCells(numDebugCells);
    for (auto& cell : debugCells) {
        cell.cellID = 0;
        cell.vertexCount = 0;
        cell.triangleCount = 0;
        cell.volume = 0.0f;
    }

    bufferSize = sizeof(voronoi::DebugCellGeometry) * numDebugCells;
    if (!tryCreateStorageBuffer(
            "debug cell geometry",
            debugCells.data(),
            bufferSize,
            resources.debugCellGeometryBuffer,
            resources.debugCellGeometryBufferOffset,
            &resources.mappedDebugCellGeometryData)) {
        return false;
    }

    uint32_t dumpCount = debugEnable ? voronoi::DEBUG_DUMP_CELL_COUNT : 1u;
    std::vector<voronoi::DumpInfo> dumpInfos(dumpCount);
    std::memset(dumpInfos.data(), 0, sizeof(voronoi::DumpInfo) * dumpCount);

    bufferSize = sizeof(voronoi::DumpInfo) * dumpCount;
    if (!tryCreateStorageBuffer(
            "voronoi dump",
            dumpInfos.data(),
            bufferSize,
            resources.voronoiDumpBuffer,
            resources.voronoiDumpBufferOffset,
            &resources.mappedVoronoiDumpData)) {
        return false;
    }

    bufferSize = sizeof(glm::vec4) * seedPositions.size();
    void* mappedSeeds = nullptr;
    if (!tryCreateStorageBuffer(
            "seed positions",
            seedPositions.data(),
            bufferSize,
            resources.seedPositionBuffer,
            resources.seedPositionBufferOffset,
            &mappedSeeds)) {
        return false;
    }
    resources.mappedSeedPositionData = mappedSeeds;

    bufferSize = sizeof(uint32_t) * seedFlags.size();
    void* mappedFlags = nullptr;
    if (!tryCreateStorageBuffer(
            "seed flags",
            seedFlags.data(),
            bufferSize,
            resources.seedFlagsBuffer,
            resources.seedFlagsBufferOffset,
            &mappedFlags)) {
        return false;
    }
    resources.mappedSeedFlagsData = mappedFlags;

    return true;
}

bool VoronoiBuilder::buildGMLSInterfaceBuffer(uint32_t maxNeighbors) {
    if (resources.voronoiNodeCount == 0) {
        std::cerr << "[VoronoiBuilder] Error: No Voronoi nodes to build GMLS interface buffer" << std::endl;
        return false;
    }

    float* areas = static_cast<float*>(resources.mappedInterfaceAreasData);
    uint32_t* neighborIds = static_cast<uint32_t*>(resources.mappedInterfaceNeighborIdsData);
    if (!areas || !neighborIds) {
        std::cerr << "[VoronoiBuilder] Error: Interface buffers not mapped" << std::endl;
        return false;
    }

    voronoi::Node* nodes = static_cast<voronoi::Node*>(resources.mappedVoronoiNodeData);
    if (!nodes) {
        std::cerr << "[VoronoiBuilder] Error: Voronoi node buffer not mapped" << std::endl;
        return false;
    }

    const glm::vec4* seedPositions = static_cast<const glm::vec4*>(resources.mappedSeedPositionData);
    const uint32_t* seedFlags = static_cast<const uint32_t*>(resources.mappedSeedFlagsData);
    if (!seedPositions || !seedFlags) {
        std::cerr << "[VoronoiBuilder] Error: Seed buffers not mapped" << std::endl;
        return false;
    }

    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    freeBuffer(resources.gmlsInterfaceBuffer, resources.gmlsInterfaceBufferOffset);

    std::vector<voronoi::GMLSInterface> interfaces;
    interfaces.reserve(static_cast<size_t>(resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors));

    uint32_t totalNeighbors = 0;
    uint32_t invalidNeighborIndexCount = 0;
    double minConductance = std::numeric_limits<double>::max();
    double maxConductance = 0.0;
    double sumConductance = 0.0;

    for (uint32_t cellIdx = 0; cellIdx < resources.voronoiNodeCount; ++cellIdx) {
        uint32_t neighborOffset = totalNeighbors;
        uint32_t validNeighborCount = 0;
        if (seedFlags && (seedFlags[cellIdx] & 1u) != 0u) {
            nodes[cellIdx].neighborOffset = neighborOffset;
            nodes[cellIdx].neighborCount = 0;
            continue;
        }

        uint32_t interfaceCount = nodes[cellIdx].interfaceNeighborCount;
        if (interfaceCount > maxNeighbors) {
            interfaceCount = maxNeighbors;
        }

        const glm::vec3 cellPosition(seedPositions[cellIdx]);

        for (uint32_t k = 0; k < interfaceCount; ++k) {
            uint32_t idx = cellIdx * maxNeighbors + k;
            uint32_t neighborIdx = neighborIds[idx];
            float area = areas[idx];
            if (neighborIdx == UINT32_MAX || area <= 0.0f) {
                continue;
            }
            if (neighborIdx >= resources.voronoiNodeCount) {
                ++invalidNeighborIndexCount;
                continue;
            }
            if (seedFlags && (seedFlags[neighborIdx] & 1u) != 0u) {
                continue;
            }

            const glm::vec3 seedB(seedPositions[neighborIdx]);
            const float distance = glm::length(seedB - cellPosition);
            if (distance <= 1e-12f || area <= 1e-8f) {
                continue;
            }

            // FTPA conductance: area/distance is exact for the normal gradient
            // on Voronoi meshes (perpendicular bisector property).
            const float conductance = area / distance;

            minConductance = std::min(minConductance, static_cast<double>(conductance));
            maxConductance = std::max(maxConductance, static_cast<double>(conductance));
            sumConductance += static_cast<double>(conductance);

            voronoi::GMLSInterface interfaceSample{};
            interfaceSample.neighborIdx = neighborIdx;
            interfaceSample.conductance = conductance;

            interfaces.push_back(interfaceSample);
            ++validNeighborCount;
            ++totalNeighbors;
        }

        nodes[cellIdx].neighborOffset = neighborOffset;
        nodes[cellIdx].neighborCount = validNeighborCount;
    }

    if (invalidNeighborIndexCount > 0) {
        std::cerr << "[VoronoiBuilder] Warning: discarded " << invalidNeighborIndexCount
                  << " interface neighbors with out-of-range seed indices" << std::endl;
    }

    if (interfaces.empty()) {
        std::cerr << "[VoronoiBuilder] Failed to build any interface samples" << std::endl;
        return false;
    }

    {
        VkDeviceSize bufferSize = sizeof(voronoi::GMLSInterface) * interfaces.size();
        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                memoryAllocator,
                vulkanDevice,
                interfaces.data(),
                bufferSize,
                resources.gmlsInterfaceBuffer,
                resources.gmlsInterfaceBufferOffset,
                &mappedPtr) != VK_SUCCESS ||
            resources.gmlsInterfaceBuffer == VK_NULL_HANDLE) {
            std::cerr << "[VoronoiBuilder] Failed to create GMLS interface buffer" << std::endl;
            return false;
        }
    }

    return true;
}

bool VoronoiBuilder::rebuildOccupancyPointBuffer(const std::vector<VoronoiDomain>& domains) const {
    if (resources.occupancyPointBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.occupancyPointBuffer, resources.occupancyPointBufferOffset);
        resources.occupancyPointBuffer = VK_NULL_HANDLE;
        resources.occupancyPointBufferOffset = 0;
    }
    resources.occupancyPointCount = 0;

    size_t estimatedPointCount = 0;
    for (const VoronoiDomain& domain : domains) {
        if (!domain.voxelGridBuilt) {
            continue;
        }
        estimatedPointCount += domain.voxelGrid.getOccupancyData().size() / 4;
    }

    std::vector<PointRenderer::PointVertex> points;
    points.reserve(estimatedPointCount);

    for (const VoronoiDomain& domain : domains) {
        if (!domain.voxelGridBuilt || !domain.modelRuntime) {
            continue;
        }

        const VoxelGrid& voxelGrid = domain.voxelGrid;
        const auto& occupancy = voxelGrid.getOccupancyData();
        const auto& params = voxelGrid.getParams();
        const glm::mat4 modelMatrix = domain.modelRuntime->getModelMatrix();
        const int dimX = params.gridDim.x;
        const int dimY = params.gridDim.y;
        const int dimZ = params.gridDim.z;
        const int stride = dimX + 1;

        for (int z = 0; z <= dimZ; ++z) {
            for (int y = 0; y <= dimY; ++y) {
                for (int x = 0; x <= dimX; ++x) {
                    const size_t idx =
                        static_cast<size_t>(z) * static_cast<size_t>(stride) * static_cast<size_t>(stride) +
                        static_cast<size_t>(y) * static_cast<size_t>(stride) +
                        static_cast<size_t>(x);
                    if (idx >= occupancy.size()) {
                        continue;
                    }

                    const uint8_t occ = occupancy[idx];
                    if (occ == 0) {
                        continue;
                    }

                    const glm::vec3 localPos = voxelGrid.getCornerPosition(x, y, z);
                    const glm::vec3 worldPos = glm::vec3(modelMatrix * glm::vec4(localPos, 1.0f));
                    glm::vec3 color = glm::vec3(0.2f, 1.0f, 0.2f);
                    if (occ == 1) {
                        color = glm::vec3(1.0f, 0.2f, 0.2f);
                    }
                    points.push_back({ worldPos, color });
                }
            }
        }
    }

    if (points.empty()) {
        return true;
    }

    const VkDeviceSize bufferSize = sizeof(PointRenderer::PointVertex) * points.size();
    auto [buffer, offset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        alignof(PointRenderer::PointVertex));
    if (buffer == VK_NULL_HANDLE) {
        return false;
    }

    void* mappedPtr = memoryAllocator.getMappedPointer(buffer, offset);
    if (!mappedPtr) {
        memoryAllocator.free(buffer, offset);
        return false;
    }

    std::memcpy(mappedPtr, points.data(), static_cast<size_t>(bufferSize));
    resources.occupancyPointBuffer = buffer;
    resources.occupancyPointBufferOffset = offset;
    resources.occupancyPointCount = static_cast<uint32_t>(points.size());
    return true;
}

bool VoronoiBuilder::generateDiagram(
    std::vector<VoronoiDomain>& receiverVoronoiDomains,
    bool debugEnable,
    uint32_t maxNeighbors,
    VoronoiGeoCompute* voronoiGeoCompute) {
    if (receiverVoronoiDomains.empty()) {
        std::cerr << "[VoronoiBuilder] Cannot generate Voronoi diagram: no receiver domains" << std::endl;
        return false;
    }

    uint32_t expectedOffset = 0;
    for (const VoronoiDomain& domain : receiverVoronoiDomains) {
        if (!domain.integrator) {
            std::cerr << "[VoronoiBuilder] Receiver domain missing integrator for model "
                      << domain.receiverModelId << std::endl;
            return false;
        }

        const auto& domainSeeds = domain.integrator->getSeedPositions();
        if (domainSeeds.empty()) {
            std::cerr << "[VoronoiBuilder] Receiver domain has no seeds for model "
                      << domain.receiverModelId << std::endl;
            return false;
        }
        if (domain.seedFlags.size() != domainSeeds.size()) {
            std::cerr << "[VoronoiBuilder] Receiver domain seed flag mismatch for model "
                      << domain.receiverModelId << std::endl;
            return false;
        }
        if (domain.nodeCount != static_cast<uint32_t>(domainSeeds.size())) {
            std::cerr << "[VoronoiBuilder] Receiver domain node count mismatch for model "
                      << domain.receiverModelId << std::endl;
            return false;
        }
        if (domain.nodeOffset != expectedOffset) {
            std::cerr << "[VoronoiBuilder] Receiver domain offset mismatch for model "
                      << domain.receiverModelId << std::endl;
            return false;
        }
        expectedOffset += domain.nodeCount;
    }

    resources.voronoiNodeCount = expectedOffset;
    if (resources.voronoiNodeCount == 0) {
        std::cerr << "[VoronoiBuilder] Cannot generate Voronoi diagram: total node count is zero" << std::endl;
        return false;
    }

    std::vector<voronoi::Node> initialNodes(resources.voronoiNodeCount);
    for (voronoi::Node& node : initialNodes) {
        node.volume = 0.0f;
        node.neighborOffset = 0u;
        node.neighborCount = 0u;
        node.interfaceNeighborCount = 0u;
    }

    std::vector<glm::vec4> globalSeedPositions;
    std::vector<uint32_t> globalSeedFlags;
    std::vector<uint32_t> globalNeighborIndices;
    globalSeedPositions.reserve(resources.voronoiNodeCount);
    globalSeedFlags.reserve(resources.voronoiNodeCount);
    globalNeighborIndices.reserve(
        static_cast<size_t>(resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors));

    for (const VoronoiDomain& domain : receiverVoronoiDomains) {
        const auto& domainSeeds = domain.integrator->getSeedPositions();
        const auto& domainNeighbors = domain.integrator->getNeighborIndices();
        const size_t expectedNeighborCount =
            static_cast<size_t>(domain.nodeCount) * static_cast<size_t>(maxNeighbors);
        if (domainNeighbors.size() < expectedNeighborCount) {
            std::cerr << "[VoronoiBuilder] Receiver domain neighbor buffer too small for model "
                      << domain.receiverModelId << std::endl;
            return false;
        }

        globalSeedPositions.insert(globalSeedPositions.end(), domainSeeds.begin(), domainSeeds.end());
        globalSeedFlags.insert(globalSeedFlags.end(), domain.seedFlags.begin(), domain.seedFlags.end());

        for (uint32_t localNodeIndex = 0; localNodeIndex < domain.nodeCount; ++localNodeIndex) {
            const size_t base = static_cast<size_t>(localNodeIndex) * static_cast<size_t>(maxNeighbors);
            for (uint32_t k = 0; k < maxNeighbors; ++k) {
                uint32_t neighborIndex = domainNeighbors[base + static_cast<size_t>(k)];
                if (neighborIndex != UINT32_MAX) {
                    if (neighborIndex < domain.nodeCount) {
                        neighborIndex += domain.nodeOffset;
                    } else {
                        neighborIndex = UINT32_MAX;
                    }
                }
                globalNeighborIndices.push_back(neighborIndex);
            }
        }

    }

    if (globalSeedPositions.size() != resources.voronoiNodeCount ||
        globalSeedFlags.size() != resources.voronoiNodeCount) {
        std::cerr << "[VoronoiBuilder] Internal error: packed Voronoi buffers do not match node count" << std::endl;
        return false;
    }

    if (!createVoronoiGeometryBuffers(
            initialNodes,
            globalSeedPositions,
            globalSeedFlags,
            globalNeighborIndices,
            debugEnable,
            maxNeighbors)) {
        return false;
    }

    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    if (voronoiGeoCompute) {
        voronoiGeoCompute->initialize(resources.voronoiNodeCount);
    }

    for (const VoronoiDomain& domain : receiverVoronoiDomains) {
        if (!domain.integrator || domain.nodeCount == 0) {
            continue;
        }

        void* mappedPtr = nullptr;
        freeBuffer(resources.meshTriangleBuffer, resources.meshTriangleBufferOffset);
        freeBuffer(resources.voxelGridParamsBuffer, resources.voxelGridParamsBufferOffset);
        freeBuffer(resources.voxelOccupancyBuffer, resources.voxelOccupancyBufferOffset);
        freeBuffer(resources.voxelTrianglesListBuffer, resources.voxelTrianglesListBufferOffset);
        freeBuffer(resources.voxelOffsetsBuffer, resources.voxelOffsetsBufferOffset);

        std::vector<MeshTriangleGPU> meshTris = domain.integrator->getMeshTriangles();
        if (meshTris.empty()) {
            meshTris.push_back({});
        }

        VkDeviceSize bufferSize = sizeof(MeshTriangleGPU) * meshTris.size();
        if (!tryCreateStorageBuffer(
                "mesh triangles",
                meshTris.data(),
                bufferSize,
                resources.meshTriangleBuffer,
                resources.meshTriangleBufferOffset,
                &mappedPtr)) {
            return false;
        }

        VoxelGrid::VoxelGridParams params{};
        std::vector<uint32_t> occupancy32;
        std::vector<int32_t> trianglesList;
        std::vector<int32_t> offsets;

        if (domain.voxelGridBuilt) {
            params = domain.voxelGrid.getParams();
            const auto& occupancy8 = domain.voxelGrid.getOccupancyData();
            occupancy32.resize(occupancy8.size());
            for (size_t i = 0; i < occupancy8.size(); ++i) {
                occupancy32[i] = static_cast<uint32_t>(occupancy8[i]);
            }
            trianglesList = domain.voxelGrid.getTrianglesList();
            offsets = domain.voxelGrid.getOffsets();
        } else {
            params.gridMin = glm::vec3(0.0f);
            params.cellSize = 1.0f;
            params.gridDim = glm::ivec3(1);
            params.totalCells = 1;
        }

        if (occupancy32.empty()) {
            occupancy32.push_back(0u);
        }
        if (trianglesList.empty()) {
            trianglesList.push_back(-1);
        }
        if (offsets.empty()) {
            offsets.push_back(0);
        }

        bufferSize = sizeof(VoxelGrid::VoxelGridParams);
        if (createUniformBuffer(
                memoryAllocator,
                vulkanDevice,
                bufferSize,
                resources.voxelGridParamsBuffer,
                resources.voxelGridParamsBufferOffset,
                &mappedPtr) != VK_SUCCESS ||
            resources.voxelGridParamsBuffer == VK_NULL_HANDLE ||
            mappedPtr == nullptr) {
            std::cerr << "[VoronoiBuilder] Failed to create voxel grid params buffer" << std::endl;
            return false;
        }
        std::memcpy(mappedPtr, &params, sizeof(VoxelGrid::VoxelGridParams));

        bufferSize = sizeof(uint32_t) * occupancy32.size();
        if (!tryCreateStorageBuffer(
                "voxel occupancy",
                occupancy32.data(),
                bufferSize,
                resources.voxelOccupancyBuffer,
                resources.voxelOccupancyBufferOffset,
                &mappedPtr,
                true)) {
            return false;
        }

        bufferSize = sizeof(int32_t) * trianglesList.size();
        if (!tryCreateStorageBuffer(
                "voxel triangle list",
                trianglesList.data(),
                bufferSize,
                resources.voxelTrianglesListBuffer,
                resources.voxelTrianglesListBufferOffset,
                &mappedPtr,
                true)) {
            return false;
        }

        bufferSize = sizeof(int32_t) * offsets.size();
        if (!tryCreateStorageBuffer(
                "voxel offsets",
                offsets.data(),
                bufferSize,
                resources.voxelOffsetsBuffer,
                resources.voxelOffsetsBufferOffset,
                &mappedPtr,
                true)) {
            return false;
        }

        if (voronoiGeoCompute) {
            VoronoiGeoCompute::Bindings bindings{};
            bindings.voronoiNodeBuffer = resources.voronoiNodeBuffer;
            bindings.voronoiNodeBufferOffset = resources.voronoiNodeBufferOffset;
            bindings.voronoiNodeBufferRange = sizeof(voronoi::Node) * resources.voronoiNodeCount;
            bindings.meshTriangleBuffer = resources.meshTriangleBuffer;
            bindings.meshTriangleBufferOffset = resources.meshTriangleBufferOffset;
            bindings.seedPositionBuffer = resources.seedPositionBuffer;
            bindings.seedPositionBufferOffset = resources.seedPositionBufferOffset;
            bindings.voxelGridParamsBuffer = resources.voxelGridParamsBuffer;
            bindings.voxelGridParamsBufferOffset = resources.voxelGridParamsBufferOffset;
            bindings.voxelGridParamsBufferRange = sizeof(VoxelGrid::VoxelGridParams);
            bindings.voxelOccupancyBuffer = resources.voxelOccupancyBuffer;
            bindings.voxelOccupancyBufferOffset = resources.voxelOccupancyBufferOffset;
            bindings.voxelTrianglesListBuffer = resources.voxelTrianglesListBuffer;
            bindings.voxelTrianglesListBufferOffset = resources.voxelTrianglesListBufferOffset;
            bindings.voxelOffsetsBuffer = resources.voxelOffsetsBuffer;
            bindings.voxelOffsetsBufferOffset = resources.voxelOffsetsBufferOffset;
            bindings.neighborIndicesBuffer = resources.neighborIndicesBuffer;
            bindings.neighborIndicesBufferOffset = resources.neighborIndicesBufferOffset;
            bindings.interfaceAreasBuffer = resources.interfaceAreasBuffer;
            bindings.interfaceAreasBufferOffset = resources.interfaceAreasBufferOffset;
            bindings.interfaceNeighborIdsBuffer = resources.interfaceNeighborIdsBuffer;
            bindings.interfaceNeighborIdsBufferOffset = resources.interfaceNeighborIdsBufferOffset;
            bindings.debugCellGeometryBuffer = resources.debugCellGeometryBuffer;
            bindings.debugCellGeometryBufferOffset = resources.debugCellGeometryBufferOffset;
            bindings.seedFlagsBuffer = resources.seedFlagsBuffer;
            bindings.seedFlagsBufferOffset = resources.seedFlagsBufferOffset;
            bindings.voronoiDumpBuffer = resources.voronoiDumpBuffer;
            bindings.voronoiDumpBufferOffset = resources.voronoiDumpBufferOffset;

            voronoiGeoCompute->updateDescriptors(bindings);
            VoronoiGeoCompute::PushConstants geoPushConstants{};
            geoPushConstants.debugEnable = debugEnable ? 1u : 0u;
            geoPushConstants.nodeOffset = domain.nodeOffset;
            geoPushConstants.nodeCount = domain.nodeCount;
            voronoiGeoCompute->dispatch(geoPushConstants);
        }
    }

    freeBuffer(resources.meshTriangleBuffer, resources.meshTriangleBufferOffset);
    freeBuffer(resources.voxelGridParamsBuffer, resources.voxelGridParamsBufferOffset);
    freeBuffer(resources.voxelOccupancyBuffer, resources.voxelOccupancyBufferOffset);
    freeBuffer(resources.voxelTrianglesListBuffer, resources.voxelTrianglesListBufferOffset);
    freeBuffer(resources.voxelOffsetsBuffer, resources.voxelOffsetsBufferOffset);

    setGhost(receiverVoronoiDomains, true);

    if (debugEnable) {
        std::ofstream seedMapFile("cell_seed_positions.txt");
        seedMapFile << "# Cell Index -> Seed Position\n";
        seedMapFile << "# Seed positions (cells.size() = " << globalSeedPositions.size() << ")\n";
        for (size_t i = 0; i < globalSeedPositions.size(); ++i) {
            const auto& pos = globalSeedPositions[i];
            seedMapFile << "Cell " << i << " -> Seed at (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        }
        seedMapFile.close();
    }

    if (!buildGMLSInterfaceBuffer(maxNeighbors)) {
        return false;
    }

    return rebuildOccupancyPointBuffer(receiverVoronoiDomains);
}

bool VoronoiBuilder::stageSurfaceMappings(std::vector<VoronoiDomain>& receiverVoronoiDomains) const {
    for (VoronoiDomain& domain : receiverVoronoiDomains) {
        if (!domain.modelRuntime || !domain.integrator) {
            std::cerr << "[VoronoiBuilder] Skipping surface mapping for runtimeModelId="
                      << domain.receiverModelId
                      << " modelRuntime=" << (domain.modelRuntime ? "present" : "missing")
                      << " integrator=" << (domain.integrator ? "present" : "missing")
                      << std::endl;
            continue;
        }

        const auto& surfacePoints = domain.modelRuntime->getIntrinsicSurfacePositions();
        if (surfacePoints.empty()) {
            std::cerr << "[VoronoiBuilder] Skipping surface mapping for runtimeModelId="
                      << domain.receiverModelId
                      << " because intrinsic surface point list is empty"
                      << std::endl;
            continue;
        }

        const auto& domainSeedPositions4 = domain.integrator->getSeedPositions();
        if (domainSeedPositions4.empty() || domain.seedFlags.size() != domainSeedPositions4.size()) {
            continue;
        }

        std::vector<glm::vec3> regularSeedPositions;
        std::vector<uint32_t> regularGlobalCellIndices;
        regularSeedPositions.reserve(domainSeedPositions4.size());
        regularGlobalCellIndices.reserve(domainSeedPositions4.size());
        for (uint32_t localCellIndex = 0; localCellIndex < static_cast<uint32_t>(domainSeedPositions4.size()); ++localCellIndex) {
            if ((domain.seedFlags[localCellIndex] & 1u) != 0u) {
                continue;
            }

            const glm::vec4& seed = domainSeedPositions4[localCellIndex];
            regularSeedPositions.push_back(glm::vec3(seed));
            regularGlobalCellIndices.push_back(domain.nodeOffset + localCellIndex);
        }
        if (regularSeedPositions.size() < 4) {
            continue;
        }

        SeedPointCloudAdapter cloud{ regularSeedPositions };
        KDTree index(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
        index.buildIndex();

        const size_t supportCount = std::min<size_t>(32, regularSeedPositions.size());

        std::vector<voronoi::GMLSSurfaceStencil> stencils(surfacePoints.size());
        std::vector<voronoi::GMLSSurfaceWeight> valueWeights;
        std::vector<voronoi::GMLSSurfaceGradientWeight> gradientWeights;
        valueWeights.reserve(surfacePoints.size() * supportCount);
        gradientWeights.reserve(surfacePoints.size() * supportCount);

        std::vector<size_t> retIndices(supportCount, 0);
        std::vector<float> outDistSq(supportCount, 0.0f);
        std::vector<glm::dvec3> sourcePositions;
        std::vector<double> valueWeightDoubles;
        std::vector<glm::dvec3> gradientWeightTriples;
        sourcePositions.reserve(supportCount);

        for (size_t vertexIndex = 0; vertexIndex < surfacePoints.size(); ++vertexIndex) {
            const glm::vec3& point = surfacePoints[vertexIndex];
            const float query[3] = { point.x, point.y, point.z };

            nanoflann::KNNResultSet<float> resultSet(supportCount);
            resultSet.init(retIndices.data(), outDistSq.data());
            index.findNeighbors(resultSet, query);

            sourcePositions.clear();
            float maxDistSq = 0.0f;
            for (size_t neighborIndex = 0; neighborIndex < supportCount; ++neighborIndex) {
                sourcePositions.push_back(glm::dvec3(regularSeedPositions[retIndices[neighborIndex]]));
                if (outDistSq[neighborIndex] > maxDistSq) maxDistSq = outDistSq[neighborIndex];
            }
            const double kernelRadius = std::max<double>(static_cast<double>(std::sqrt(maxDistSq)) * 2.0, 1e-5);
            const bool validWeights = GMLS::computeWeights(
                glm::dvec3(point),
                sourcePositions,
                kernelRadius,
                valueWeightDoubles,
                gradientWeightTriples);

            voronoi::GMLSSurfaceStencil& stencil = stencils[vertexIndex];
            stencil.valueWeightOffset = static_cast<uint32_t>(valueWeights.size());
            stencil.gradientWeightOffset = static_cast<uint32_t>(gradientWeights.size());
            stencil.valueWeightCount = 0;
            stencil.gradientWeightCount = 0;

            if (!validWeights) {
                continue;
            }

            for (size_t neighborIndex = 0; neighborIndex < supportCount; ++neighborIndex) {
                const uint32_t globalCellIndex = regularGlobalCellIndices[retIndices[neighborIndex]];
                const float valueWeight = static_cast<float>(valueWeightDoubles[neighborIndex]);
                const glm::dvec3 gradientWeight = gradientWeightTriples[neighborIndex];

                if (std::abs(valueWeight) > 1e-7f) {
                    valueWeights.push_back({ globalCellIndex, valueWeight, 0u, 0u });
                    ++stencil.valueWeightCount;
                }

                if (glm::dot(gradientWeight, gradientWeight) > 1e-14) {
                    gradientWeights.push_back({
                        globalCellIndex,
                        static_cast<float>(gradientWeight.x),
                        static_cast<float>(gradientWeight.y),
                        static_cast<float>(gradientWeight.z)
                    });
                    ++stencil.gradientWeightCount;
                }
            }
        }

        domain.modelRuntime->stageGMLSSurfaceData(stencils, valueWeights, gradientWeights);
    }

    return true;
}
