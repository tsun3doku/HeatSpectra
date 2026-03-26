#include "VoronoiBuilder.hpp"

#include "voronoi/VoronoiModelRuntime.hpp"

#include "renderers/PointRenderer.hpp"
#include "util/Structs.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiGeoCompute.hpp"

#include <cstring>
#include <fstream>
#include "mesh/remesher/Remesher.hpp"
#include "scene/Model.hpp"

#include <iostream>
#include <unordered_set>

namespace {

SupportingHalfedge::IntrinsicMesh toIntrinsicMesh(const IntrinsicMeshData& intrinsic) {
    SupportingHalfedge::IntrinsicMesh mesh{};
    mesh.vertices.reserve(intrinsic.vertices.size());
    for (const IntrinsicMeshVertexData& vertex : intrinsic.vertices) {
        SupportingHalfedge::IntrinsicVertex converted{};
        converted.intrinsicVertexId = vertex.intrinsicVertexId;
        converted.position = glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]);
        converted.normal = glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
        converted.inputLocationType = vertex.inputLocationType;
        converted.inputElementId = vertex.inputElementId;
        converted.inputBaryCoords = glm::vec3(
            vertex.inputBaryCoords[0],
            vertex.inputBaryCoords[1],
            vertex.inputBaryCoords[2]);
        mesh.vertices.push_back(converted);
    }
    mesh.indices = intrinsic.triangleIndices;
    mesh.faceIds = intrinsic.faceIds;
    mesh.triangles.reserve(intrinsic.triangles.size());
    for (const IntrinsicMeshTriangleData& triangle : intrinsic.triangles) {
        SupportingHalfedge::IntrinsicTriangle converted{};
        converted.center = glm::vec3(triangle.center[0], triangle.center[1], triangle.center[2]);
        converted.normal = glm::vec3(triangle.normal[0], triangle.normal[1], triangle.normal[2]);
        converted.area = triangle.area;
        converted.vertexIndices[0] = triangle.vertexIndices[0];
        converted.vertexIndices[1] = triangle.vertexIndices[1];
        converted.vertexIndices[2] = triangle.vertexIndices[2];
        converted.faceId = triangle.faceId;
        mesh.triangles.push_back(converted);
    }
    return mesh;
}

}

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
    const VoronoiParams& params,
    uint32_t maxNeighbors) const {
    receiverVoronoiDomains.clear();

    std::unordered_set<uint32_t> seenReceiverModelIds;
    for (const auto& modelRuntimePtr : modelRuntimes) {
        VoronoiModelRuntime* modelRuntime = modelRuntimePtr.get();
        if (!modelRuntime) {
            continue;
        }

        Model& receiverModel = modelRuntime->getModel();
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

        const SupportingHalfedge::IntrinsicMesh intrinsicMesh = toIntrinsicMesh(modelRuntime->getIntrinsicMeshData());
        domain.seeder->generateSeeds(
            intrinsicMesh,
            receiverModel,
            params.cellSize,
            domain.voxelGrid,
            params.voxelResolution);
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
            if (seed.isGhost) {
                flags |= 1u;
            }
            if (seed.isSurface) {
                flags |= 2u;
            }
            domain.seedFlags.push_back(flags);
        }

        domain.integrator->computeNeighbors(seedPositions, maxNeighbors);
        domain.integrator->extractMeshTriangles(receiverModel);
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

bool VoronoiBuilder::createVoronoiGeometryBuffers(
    const std::vector<VoronoiNode>& initialNodes,
    const std::vector<glm::vec4>& seedPositions,
    const std::vector<uint32_t>& seedFlags,
    const std::vector<uint32_t>& neighborIndices,
    bool debugEnable,
    uint32_t maxNeighbors) {
    std::cout << "[VoronoiBuilder] Creating Voronoi geometry buffers..." << std::endl;
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

    VkDeviceSize bufferSize = sizeof(VoronoiNode) * resources.voronoiNodeCount;
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
    std::vector<DebugCellGeometry> debugCells(numDebugCells);
    for (auto& cell : debugCells) {
        cell.cellID = 0;
        cell.vertexCount = 0;
        cell.triangleCount = 0;
        cell.volume = 0.0f;
    }

    bufferSize = sizeof(DebugCellGeometry) * numDebugCells;
    if (!tryCreateStorageBuffer(
            "debug cell geometry",
            debugCells.data(),
            bufferSize,
            resources.debugCellGeometryBuffer,
            resources.debugCellGeometryBufferOffset,
            &resources.mappedDebugCellGeometryData)) {
        return false;
    }

    uint32_t dumpCount = debugEnable ? DEBUG_DUMP_CELL_COUNT : 1u;
    std::vector<VoronoiDumpInfo> dumpInfos(dumpCount);
    std::memset(dumpInfos.data(), 0, sizeof(VoronoiDumpInfo) * dumpCount);

    bufferSize = sizeof(VoronoiDumpInfo) * dumpCount;
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

bool VoronoiBuilder::buildVoronoiNeighborBuffer(uint32_t maxNeighbors) {
    if (resources.voronoiNodeCount == 0) {
        std::cerr << "[VoronoiBuilder] Error: No Voronoi nodes to build neighbor buffer" << std::endl;
        return false;
    }

    float* areas = static_cast<float*>(resources.mappedInterfaceAreasData);
    uint32_t* neighborIds = static_cast<uint32_t*>(resources.mappedInterfaceNeighborIdsData);
    if (!areas || !neighborIds) {
        std::cerr << "[VoronoiBuilder] Error: Interface buffers not mapped" << std::endl;
        return false;
    }

    VoronoiNode* nodes = static_cast<VoronoiNode*>(resources.mappedVoronoiNodeData);
    if (!nodes) {
        std::cerr << "[VoronoiBuilder] Error: Voronoi node buffer not mapped" << std::endl;
        return false;
    }

    const glm::vec4* seedPositions = static_cast<const glm::vec4*>(resources.mappedSeedPositionData);
    const uint32_t* seedFlags = static_cast<const uint32_t*>(resources.mappedSeedFlagsData);
    std::vector<VoronoiNeighbor> neighbors;
    neighbors.reserve(static_cast<size_t>(resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors));

    uint32_t totalNeighbors = 0;
    uint32_t invalidNeighborIndexCount = 0;
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

            float areaOverDistance = 0.0f;
            if (seedPositions) {
                const glm::vec4& seedA4 = seedPositions[cellIdx];
                const glm::vec4& seedB4 = seedPositions[neighborIdx];
                const glm::vec3 seedA(seedA4.x, seedA4.y, seedA4.z);
                const glm::vec3 seedB(seedB4.x, seedB4.y, seedB4.z);
                const float distance = glm::distance(seedA, seedB);
                if (distance > 1e-12f && area > 1e-8f) {
                    areaOverDistance = area / distance;
                }
            }

            if (areaOverDistance <= 0.0f) {
                continue;
            }

            VoronoiNeighbor neighbor{};
            neighbor.neighborIndex = neighborIdx;
            neighbor.areaOverDistance = areaOverDistance;
            neighbors.push_back(neighbor);
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

    if (totalNeighbors > 0) {
        VkDeviceSize bufferSize = sizeof(VoronoiNeighbor) * totalNeighbors;
        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                memoryAllocator,
                vulkanDevice,
                neighbors.data(),
                bufferSize,
                resources.voronoiNeighborBuffer,
                resources.voronoiNeighborBufferOffset,
                &mappedPtr) != VK_SUCCESS ||
            resources.voronoiNeighborBuffer == VK_NULL_HANDLE) {
            std::cerr << "[VoronoiBuilder] Failed to create Voronoi neighbor buffer" << std::endl;
            return false;
        }
    }

    return true;
}

void VoronoiBuilder::uploadOccupancyPoints(
    const std::vector<VoronoiDomain>& domains,
    PointRenderer* pointRenderer) const {
    if (!pointRenderer) {
        return;
    }

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
        const glm::mat4 modelMatrix = domain.modelRuntime->getModel().getModelMatrix();
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

    pointRenderer->uploadPoints(points);
}

bool VoronoiBuilder::generateDiagram(
    std::vector<VoronoiDomain>& receiverVoronoiDomains,
    bool debugEnable,
    uint32_t maxNeighbors,
    VoronoiGeoCompute* voronoiGeoCompute,
    PointRenderer* pointRenderer) {
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

    std::vector<VoronoiNode> initialNodes(resources.voronoiNodeCount);
    for (VoronoiNode& node : initialNodes) {
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

        for (uint32_t localNodeIndex = 0; localNodeIndex < domain.nodeCount; ++localNodeIndex) {
            if ((domain.seedFlags[localNodeIndex] & 1u) != 0u) {
                continue;
            }

            VoronoiNode& node = initialNodes[domain.nodeOffset + localNodeIndex];
            (void)node;
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
            bindings.voronoiNodeBufferRange = sizeof(VoronoiNode) * resources.voronoiNodeCount;
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

    if (!buildVoronoiNeighborBuffer(maxNeighbors)) {
        return false;
    }

    uploadOccupancyPoints(receiverVoronoiDomains, pointRenderer);
    return true;
}

bool VoronoiBuilder::stageSurfaceMappings(
    std::vector<VoronoiDomain>& receiverVoronoiDomains,
    uint32_t maxNeighbors) const {
    for (VoronoiDomain& domain : receiverVoronoiDomains) {
        if (!domain.modelRuntime || !domain.integrator) {
            std::cout << "[VoronoiBuilder] Skipping surface mapping for runtimeModelId="
                      << domain.receiverModelId
                      << " modelRuntime=" << (domain.modelRuntime ? "present" : "missing")
                      << " integrator=" << (domain.integrator ? "present" : "missing")
                      << std::endl;
            continue;
        }

        const auto& surfacePoints = domain.modelRuntime->getIntrinsicSurfacePositions();
        if (surfacePoints.empty()) {
            std::cout << "[VoronoiBuilder] Skipping surface mapping for runtimeModelId="
                      << domain.receiverModelId
                      << " because intrinsic surface point list is empty"
                      << std::endl;
            continue;
        }

        std::cout << "[VoronoiBuilder] Mapping surface points for runtimeModelId="
                  << domain.receiverModelId
                  << " surfaceVertices=" << surfacePoints.size()
                  << " seeds=" << domain.seedFlags.size()
                  << " nodeOffset=" << domain.nodeOffset
                  << " nodeCount=" << domain.nodeCount
                  << std::endl;

        std::vector<uint32_t> cellIndices;
        domain.integrator->computeSurfacePointMapping(
            surfacePoints,
            domain.seedFlags,
            maxNeighbors,
            cellIndices);

        for (uint32_t& cellIndex : cellIndices) {
            if (cellIndex == UINT32_MAX) {
                continue;
            }
            cellIndex += domain.nodeOffset;
        }

        domain.modelRuntime->stageVoronoiSurfaceMapping(cellIndices);
    }

    return true;
}
