#include "HeatSystemVoronoiStage.hpp"

#include "HeatSystemResources.hpp"

#include "HeatReceiver.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "renderers/PointRenderer.hpp"
#include "voronoi/VoronoiGeoCompute.hpp"
#include "voronoi/VoronoiIntegrator.hpp"
#include "util/Structs.hpp"
#include "util/file_utils.h"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <vector>

HeatSystemVoronoiStage::HeatSystemVoronoiStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemVoronoiStage::onResourcesRecreated(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) {
    (void)maxFramesInFlight;
    (void)extent;
    (void)renderPass;
}

bool HeatSystemVoronoiStage::buildReceiverDomains(const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
    std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains, const HeatSolveParams& solveParams, uint32_t maxNeighbors) const {
    receiverVoronoiDomains.clear();

    std::unordered_set<uint32_t> seenReceiverModelIds;
    for (const auto& receiverPtr : receivers) {
        HeatReceiver* receiver = receiverPtr.get();
        if (!receiver) {
            continue;
        }

        Model& receiverModel = receiver->getModel();
        const uint32_t receiverModelId = receiverModel.getRuntimeModelId();
        if (receiverModelId == 0 || !seenReceiverModelIds.insert(receiverModelId).second) {
            continue;
        }

        iODT* remesherForModel = context.remesher.getRemesherForModel(&receiverModel);
        if (!remesherForModel) {
            std::cerr << "[HeatSystem] Skipping receiver " << receiverModelId
                      << ": missing remesher state" << std::endl;
            continue;
        }

        SupportingHalfedge* supportingHalfedge = remesherForModel->getSupportingHalfedge();
        if (!supportingHalfedge) {
            std::cerr << "[HeatSystem] Skipping receiver " << receiverModelId
                      << ": missing supporting halfedge" << std::endl;
            continue;
        }

        HeatSystemVoronoiDomain domain{};
        domain.receiverModelId = receiverModelId;
        domain.receiver = receiver;
        domain.seeder = std::make_unique<VoronoiSeeder>();
        domain.integrator = std::make_unique<VoronoiIntegrator>();
        if (!domain.seeder || !domain.integrator) {
            std::cerr << "[HeatSystem] Skipping receiver " << receiverModelId
                      << ": failed to allocate Voronoi domain helpers" << std::endl;
            continue;
        }

        const SupportingHalfedge::IntrinsicMesh intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
        domain.seeder->generateSeeds(
            intrinsicMesh,
            receiverModel,
            solveParams.cellSize,
            domain.voxelGrid,
            solveParams.voxelResolution);
        domain.voxelGridBuilt = (domain.voxelGrid.getGridSize() > 0);

        const std::vector<VoronoiSeeder::Seed>& domainSeeds = domain.seeder->getSeeds();
        if (domainSeeds.empty()) {
            std::cerr << "[HeatSystem] Skipping receiver " << receiverModelId
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
        std::cerr << "[HeatSystem] No valid receiver Voronoi domains were created." << std::endl;
        return false;
    }

    uint32_t runningOffset = 0;
    for (HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
        domain.nodeOffset = runningOffset;
        runningOffset += domain.nodeCount;
    }

    return true;
}

bool HeatSystemVoronoiStage::tryCreateStorageBuffer(const char* label, const void* data, VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset,
    void** mapped, bool hostVisible) const {
    const VkResult result = createStorageBuffer(
        context.memoryAllocator,
        context.vulkanDevice,
        data,
        size,
        buffer,
        offset,
        mapped,
        hostVisible);

    if (result != VK_SUCCESS || buffer == VK_NULL_HANDLE) {
        std::cerr << "[HeatSystem] Failed to create " << label << " storage buffer (VkResult=" << result << ")" << std::endl;
        return false;
    }

    return true;
}

bool HeatSystemVoronoiStage::createVoronoiGeometryBuffers(const std::vector<VoronoiNodeGPU>& initialNodes, const std::vector<glm::vec4>& seedPositions,
    const std::vector<uint32_t>& seedFlags, const std::vector<uint32_t>& neighborIndices, bool debugEnable, uint32_t maxNeighbors) {
    std::cout << "[HeatSystem] Creating Voronoi geometry buffers..." << std::endl;
    if (seedPositions.empty() || seedFlags.empty() || initialNodes.empty()) {
        std::cerr << "[HeatSystem] Failed to create Voronoi buffers: empty node/seed data" << std::endl;
        return false;
    }

    if (seedPositions.size() != seedFlags.size() || seedPositions.size() != initialNodes.size()) {
        std::cerr << "[HeatSystem] Failed to create Voronoi buffers: node/seed/flag count mismatch" << std::endl;
        return false;
    }

    context.resources.voronoiNodeCount = static_cast<uint32_t>(seedPositions.size());
    void* mappedPtr = nullptr;

    VkDeviceSize bufferSize = sizeof(VoronoiNodeGPU) * context.resources.voronoiNodeCount;
    if (!tryCreateStorageBuffer(
            "voronoi node",
            initialNodes.data(),
            bufferSize,
            context.resources.voronoiNodeBuffer,
            context.resources.voronoiNodeBufferOffset_,
            &context.resources.mappedVoronoiNodeData)) {
        return false;
    }

    std::vector<uint32_t> flattenedNeighbors = neighborIndices;
    if (flattenedNeighbors.empty()) {
        flattenedNeighbors.resize(
            static_cast<size_t>(context.resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors),
            UINT32_MAX);
    }

    bufferSize = sizeof(uint32_t) * flattenedNeighbors.size();
    if (!tryCreateStorageBuffer(
            "neighbor indices",
            flattenedNeighbors.data(),
            bufferSize,
            context.resources.neighborIndicesBuffer,
            context.resources.neighborIndicesBufferOffset_,
            &mappedPtr)) {
        return false;
    }

    const size_t interfaceDataSize = static_cast<size_t>(context.resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors);
    std::vector<float> emptyAreas(interfaceDataSize, 0.0f);
    bufferSize = sizeof(float) * interfaceDataSize;

    void* mappedInterfaceAreas = nullptr;
    if (!tryCreateStorageBuffer(
            "interface areas",
            emptyAreas.data(),
            bufferSize,
            context.resources.interfaceAreasBuffer,
            context.resources.interfaceAreasBufferOffset_,
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
            context.resources.interfaceNeighborIdsBuffer,
            context.resources.interfaceNeighborIdsBufferOffset_,
            &mappedInterfaceNeighborIds)) {
        return false;
    }

    context.resources.mappedInterfaceAreasData = mappedInterfaceAreas;
    context.resources.mappedInterfaceNeighborIdsData = mappedInterfaceNeighborIds;

    uint32_t numDebugCells = 1u;
    if (debugEnable) {
        numDebugCells = context.resources.voronoiNodeCount;
    }

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
            context.resources.debugCellGeometryBuffer,
            context.resources.debugCellGeometryBufferOffset_,
            &context.resources.mappedDebugCellGeometryData)) {
        return false;
    }

    uint32_t dumpCount = 1u;
    if (debugEnable) {
        dumpCount = DEBUG_DUMP_CELL_COUNT;
    }

    std::vector<VoronoiDumpInfo> dumpInfos(dumpCount);
    std::memset(dumpInfos.data(), 0, sizeof(VoronoiDumpInfo) * dumpCount);

    bufferSize = sizeof(VoronoiDumpInfo) * dumpCount;
    if (!tryCreateStorageBuffer(
            "voronoi dump",
            dumpInfos.data(),
            bufferSize,
            context.resources.voronoiDumpBuffer,
            context.resources.voronoiDumpBufferOffset_,
            &context.resources.mappedVoronoiDumpData)) {
        return false;
    }

    bufferSize = sizeof(glm::vec4) * seedPositions.size();
    void* mappedSeeds = nullptr;
    if (!tryCreateStorageBuffer(
            "seed positions",
            seedPositions.data(),
            bufferSize,
            context.resources.seedPositionBuffer,
            context.resources.seedPositionBufferOffset_,
            &mappedSeeds)) {
        return false;
    }
    context.resources.mappedSeedPositionData = mappedSeeds;

    if (seedFlags.empty()) {
        std::cerr << "[HeatSystem] ERROR: seedFlags is empty" << std::endl;
        return false;
    }

    bufferSize = sizeof(uint32_t) * seedFlags.size();
    void* mappedFlags = nullptr;
    if (!tryCreateStorageBuffer(
            "seed flags",
            seedFlags.data(),
            bufferSize,
            context.resources.seedFlagsBuffer,
            context.resources.seedFlagsBufferOffset_,
            &mappedFlags)) {
        return false;
    }
    context.resources.mappedSeedFlagsData = mappedFlags;
    std::cout << "  Seed flags: " << seedFlags.size() << " seeds" << std::endl;

    return true;
}

bool HeatSystemVoronoiStage::buildVoronoiNeighborBuffer(uint32_t maxNeighbors) {
    if (context.resources.voronoiNodeCount == 0) {
        std::cerr << "[HeatSystem] Error: No Voronoi nodes to build neighbor buffer" << std::endl;
        return false;
    }

    float* areas = static_cast<float*>(context.resources.mappedInterfaceAreasData);
    uint32_t* neighborIds = static_cast<uint32_t*>(context.resources.mappedInterfaceNeighborIdsData);
    if (!areas || !neighborIds) {
        std::cerr << "[HeatSystem] Error: Interface buffers not mapped" << std::endl;
        return false;
    }

    VoronoiNodeGPU* nodes = static_cast<VoronoiNodeGPU*>(context.resources.mappedVoronoiNodeData);
    if (!nodes) {
        std::cerr << "[HeatSystem] Error: Voronoi node buffer not mapped" << std::endl;
        return false;
    }

    const glm::vec4* seedPositions = static_cast<const glm::vec4*>(context.resources.mappedSeedPositionData);
    const uint32_t* seedFlags = static_cast<const uint32_t*>(context.resources.mappedSeedFlagsData);
    std::vector<VoronoiNeighborGPU> neighbors;
    neighbors.reserve(static_cast<size_t>(context.resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors));

    uint32_t totalNeighbors = 0;
    uint32_t invalidNeighborIndexCount = 0;
    for (uint32_t cellIdx = 0; cellIdx < context.resources.voronoiNodeCount; cellIdx++) {
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

        for (uint32_t k = 0; k < interfaceCount; k++) {
            uint32_t idx = cellIdx * maxNeighbors + k;
            uint32_t neighborIdx = neighborIds[idx];
            float area = areas[idx];
            if (neighborIdx == UINT32_MAX || area <= 0.0f) {
                continue;
            }
            if (neighborIdx >= context.resources.voronoiNodeCount) {
                invalidNeighborIndexCount++;
                continue;
            }
            if (seedFlags && (seedFlags[neighborIdx] & 1u) != 0u) {
                continue;
            }

            float areaOverDistance = 0.0f;
            if (seedPositions) {
                const glm::vec4& seedA4 = seedPositions[cellIdx];
                const glm::vec4& seedB4 = seedPositions[neighborIdx];
                glm::vec3 seedA(seedA4.x, seedA4.y, seedA4.z);
                glm::vec3 seedB(seedB4.x, seedB4.y, seedB4.z);
                const float distance = glm::distance(seedA, seedB);
                if (distance > 1e-12f && area > 1e-8f) {
                    areaOverDistance = area / distance;
                }
            }

            if (areaOverDistance <= 0.0f) {
                continue;
            }

            VoronoiNeighborGPU neighbor{};
            neighbor.neighborIndex = neighborIdx;
            neighbor.areaOverDistance = areaOverDistance;
            neighbors.push_back(neighbor);
            validNeighborCount++;
            totalNeighbors++;
        }

        nodes[cellIdx].neighborOffset = neighborOffset;
        nodes[cellIdx].neighborCount = validNeighborCount;
    }

    if (invalidNeighborIndexCount > 0) {
        std::cerr << "[HeatSystem] Warning: discarded " << invalidNeighborIndexCount
                  << " interface neighbors with out-of-range seed indices" << std::endl;
    }

    if (totalNeighbors > 0) {
        VkDeviceSize bufferSize = sizeof(VoronoiNeighborGPU) * totalNeighbors;
        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                context.memoryAllocator,
                context.vulkanDevice,
                neighbors.data(),
                bufferSize,
                context.resources.voronoiNeighborBuffer,
                context.resources.voronoiNeighborBufferOffset_,
                &mappedPtr) != VK_SUCCESS ||
            context.resources.voronoiNeighborBuffer == VK_NULL_HANDLE) {
            std::cerr << "[HeatSystem] Failed to create Voronoi neighbor buffer" << std::endl;
            return false;
        }
    }

    return true;
}

void HeatSystemVoronoiStage::initializeVoronoi() {
    if (!context.resources.mappedTempBufferA || !context.resources.mappedTempBufferB) {
        return;
    }

    float* tempsA = static_cast<float*>(context.resources.mappedTempBufferA);
    float* tempsB = static_cast<float*>(context.resources.mappedTempBufferB);

    for (uint32_t i = 0; i < context.resources.voronoiNodeCount; i++) {
        tempsA[i] = AMBIENT_TEMPERATURE;
        tempsB[i] = AMBIENT_TEMPERATURE;
    }
}

void HeatSystemVoronoiStage::uploadOccupancyPoints(const std::vector<HeatSystemVoronoiDomain>& domains, PointRenderer* pointRenderer) const {
    if (!pointRenderer) {
        return;
    }

    size_t estimatedPointCount = 0;
    for (const HeatSystemVoronoiDomain& domain : domains) {
        if (!domain.voxelGridBuilt) {
            continue;
        }
        estimatedPointCount += domain.voxelGrid.getOccupancyData().size() / 4;
    }

    std::vector<PointRenderer::PointVertex> points;
    points.reserve(estimatedPointCount);

    uint32_t builtDomainCount = 0;
    for (const HeatSystemVoronoiDomain& domain : domains) {
        if (!domain.voxelGridBuilt || !domain.receiver) {
            continue;
        }

        const VoxelGrid& voxelGrid = domain.voxelGrid;
        const auto& occupancy = voxelGrid.getOccupancyData();
        const auto& params = voxelGrid.getParams();
        const glm::mat4 modelMatrix = domain.receiver->getModel().getModelMatrix();
        int dimX = params.gridDim.x;
        int dimY = params.gridDim.y;
        int dimZ = params.gridDim.z;
        int stride = dimX + 1;

        for (int z = 0; z <= dimZ; ++z) {
            for (int y = 0; y <= dimY; ++y) {
                for (int x = 0; x <= dimX; ++x) {
                    size_t idx = static_cast<size_t>(z) * static_cast<size_t>(stride) * static_cast<size_t>(stride)
                               + static_cast<size_t>(y) * static_cast<size_t>(stride)
                               + static_cast<size_t>(x);
                    if (idx >= occupancy.size()) {
                        continue;
                    }

                    uint8_t occ = occupancy[idx];
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

        builtDomainCount++;
    }

    pointRenderer->uploadPoints(points);
}

bool HeatSystemVoronoiStage::generateVoronoiDiagram(std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains,
    const std::unordered_map<uint32_t, HeatMaterialPresetId>& receiverMaterialPresetByModelId, bool debugEnable,
    uint32_t maxFramesInFlight,  uint32_t maxNeighbors, VoronoiGeoCompute* voronoiGeoCompute, PointRenderer* pointRenderer) {
    if (receiverVoronoiDomains.empty()) {
        std::cerr << "[HeatSystem] Cannot generate Voronoi diagram: no receiver domains" << std::endl;
        return false;
    }

    uint32_t expectedOffset = 0;
    for (const HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
        if (!domain.integrator) {
            std::cerr << "[HeatSystem] Receiver domain missing integrator for model " << domain.receiverModelId << std::endl;
            return false;
        }

        const auto& domainSeeds = domain.integrator->getSeedPositions();
        if (domainSeeds.empty()) {
            std::cerr << "[HeatSystem] Receiver domain has no seeds for model " << domain.receiverModelId << std::endl;
            return false;
        }
        if (domain.seedFlags.size() != domainSeeds.size()) {
            std::cerr << "[HeatSystem] Receiver domain seed flag mismatch for model " << domain.receiverModelId
                      << " (flags=" << domain.seedFlags.size() << ", seeds=" << domainSeeds.size() << ")" << std::endl;
            return false;
        }
        if (domain.nodeCount != static_cast<uint32_t>(domainSeeds.size())) {
            std::cerr << "[HeatSystem] Receiver domain node count mismatch for model " << domain.receiverModelId
                      << " (domain.nodeCount=" << domain.nodeCount
                      << ", seeds=" << domainSeeds.size() << ")" << std::endl;
            return false;
        }
        if (domain.nodeOffset != expectedOffset) {
            std::cerr << "[HeatSystem] Receiver domain offset mismatch for model " << domain.receiverModelId
                      << " (domain.nodeOffset=" << domain.nodeOffset
                      << ", expected=" << expectedOffset << ")" << std::endl;
            return false;
        }
        expectedOffset += domain.nodeCount;
    }

    context.resources.voronoiNodeCount = expectedOffset;
    if (context.resources.voronoiNodeCount == 0) {
        std::cerr << "[HeatSystem] Cannot generate voronoi diagram: total node count is zero" << std::endl;
        return false;
    }

    std::vector<VoronoiNodeGPU> initialNodes(context.resources.voronoiNodeCount);
    const HeatMaterialPreset& defaultPreset = heatMaterialPresetById(HeatMaterialPresetId::Aluminum);
    for (VoronoiNodeGPU& node : initialNodes) {
        node.temperature = 1.0f;
        node.conductivityPerMass = 0.0f;
        node.volume = 0.0f;
        node.thermalMass = 0.0f;
        node.density = defaultPreset.density;
        node.specificHeat = defaultPreset.specificHeat;
        node.conductivity = defaultPreset.conductivity;
        node.neighborOffset = 0u;
        node.neighborCount = 0u;
        node.interfaceNeighborCount = 0u;
    }

    std::vector<glm::vec4> globalSeedPositions;
    std::vector<uint32_t> globalSeedFlags;
    std::vector<uint32_t> globalNeighborIndices;
    globalSeedPositions.reserve(context.resources.voronoiNodeCount);
    globalSeedFlags.reserve(context.resources.voronoiNodeCount);
    globalNeighborIndices.reserve(static_cast<size_t>(context.resources.voronoiNodeCount) * static_cast<size_t>(maxNeighbors));

    for (const HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
        const auto& domainSeeds = domain.integrator->getSeedPositions();
        const auto& domainNeighbors = domain.integrator->getNeighborIndices();
        const size_t expectedNeighborCount = static_cast<size_t>(domain.nodeCount) * static_cast<size_t>(maxNeighbors);
        if (domainNeighbors.size() < expectedNeighborCount) {
            std::cerr << "[HeatSystem] Receiver domain neighbor buffer too small for model " << domain.receiverModelId
                      << " (have=" << domainNeighbors.size() << ", need=" << expectedNeighborCount << ")" << std::endl;
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

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        auto presetIt = receiverMaterialPresetByModelId.find(domain.receiverModelId);
        if (presetIt != receiverMaterialPresetByModelId.end()) {
            presetId = presetIt->second;
        }
        const HeatMaterialPreset& preset = heatMaterialPresetById(presetId);

        for (uint32_t localNodeIndex = 0; localNodeIndex < domain.nodeCount; ++localNodeIndex) {
            if ((domain.seedFlags[localNodeIndex] & 1u) != 0u) {
                continue;
            }

            VoronoiNodeGPU& node = initialNodes[domain.nodeOffset + localNodeIndex];
            node.density = preset.density;
            node.specificHeat = preset.specificHeat;
            node.conductivity = preset.conductivity;
        }
    }

    if (globalSeedPositions.size() != context.resources.voronoiNodeCount ||
        globalSeedFlags.size() != context.resources.voronoiNodeCount) {
        std::cerr << "[HeatSystem] Internal error: packed Voronoi buffers do not match node count" << std::endl;
        return false;
    }

    if (!createVoronoiGeometryBuffers(initialNodes, globalSeedPositions, globalSeedFlags, globalNeighborIndices, debugEnable, maxNeighbors)) {
        std::cerr << "[HeatSystem] Failed to create Voronoi geometry buffers" << std::endl;
        return false;
    }

    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            context.memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    std::cout << "[HeatSystem] Creating geometry precompute shader..." << std::endl;
    if (voronoiGeoCompute) {
        voronoiGeoCompute->initialize(context.resources.voronoiNodeCount);
    }

    for (const HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
        if (!domain.integrator || domain.nodeCount == 0) {
            continue;
        }

        void* mappedPtr = nullptr;
        freeBuffer(context.resources.meshTriangleBuffer, context.resources.meshTriangleBufferOffset_);
        freeBuffer(context.resources.voxelGridParamsBuffer, context.resources.voxelGridParamsBufferOffset_);
        freeBuffer(context.resources.voxelOccupancyBuffer, context.resources.voxelOccupancyBufferOffset_);
        freeBuffer(context.resources.voxelTrianglesListBuffer, context.resources.voxelTrianglesListBufferOffset_);
        freeBuffer(context.resources.voxelOffsetsBuffer, context.resources.voxelOffsetsBufferOffset_);

        std::vector<MeshTriangleGPU> meshTris = domain.integrator->getMeshTriangles();
        if (meshTris.empty()) {
            meshTris.push_back({});
        }

        VkDeviceSize bufferSize = sizeof(MeshTriangleGPU) * meshTris.size();
        if (!tryCreateStorageBuffer(
                "mesh triangles",
                meshTris.data(),
                bufferSize,
                context.resources.meshTriangleBuffer,
                context.resources.meshTriangleBufferOffset_,
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
                context.memoryAllocator,
                context.vulkanDevice,
                bufferSize,
                context.resources.voxelGridParamsBuffer,
                context.resources.voxelGridParamsBufferOffset_,
                &mappedPtr) != VK_SUCCESS ||
            context.resources.voxelGridParamsBuffer == VK_NULL_HANDLE ||
            mappedPtr == nullptr) {
            std::cerr << "[HeatSystem] Failed to create voxel grid params buffer" << std::endl;
            return false;
        }
        std::memcpy(mappedPtr, &params, sizeof(VoxelGrid::VoxelGridParams));

        bufferSize = sizeof(uint32_t) * occupancy32.size();
        if (!tryCreateStorageBuffer("voxel occupancy", occupancy32.data(), bufferSize, context.resources.voxelOccupancyBuffer, context.resources.voxelOccupancyBufferOffset_, &mappedPtr, true)) {
            return false;
        }

        bufferSize = sizeof(int32_t) * trianglesList.size();
        if (!tryCreateStorageBuffer("voxel triangle list", trianglesList.data(), bufferSize, context.resources.voxelTrianglesListBuffer, context.resources.voxelTrianglesListBufferOffset_, &mappedPtr, true)) {
            return false;
        }

        bufferSize = sizeof(int32_t) * offsets.size();
        if (!tryCreateStorageBuffer("voxel offsets", offsets.data(), bufferSize, context.resources.voxelOffsetsBuffer, context.resources.voxelOffsetsBufferOffset_, &mappedPtr, true)) {
            return false;
        }

        if (voronoiGeoCompute) {
            VoronoiGeoCompute::Bindings bindings{};
            bindings.voronoiNodeBuffer = context.resources.voronoiNodeBuffer;
            bindings.voronoiNodeBufferOffset = context.resources.voronoiNodeBufferOffset_;
            bindings.voronoiNodeBufferRange = sizeof(VoronoiNodeGPU) * context.resources.voronoiNodeCount;
            bindings.meshTriangleBuffer = context.resources.meshTriangleBuffer;
            bindings.meshTriangleBufferOffset = context.resources.meshTriangleBufferOffset_;
            bindings.seedPositionBuffer = context.resources.seedPositionBuffer;
            bindings.seedPositionBufferOffset = context.resources.seedPositionBufferOffset_;
            bindings.voxelGridParamsBuffer = context.resources.voxelGridParamsBuffer;
            bindings.voxelGridParamsBufferOffset = context.resources.voxelGridParamsBufferOffset_;
            bindings.voxelGridParamsBufferRange = sizeof(VoxelGrid::VoxelGridParams);
            bindings.voxelOccupancyBuffer = context.resources.voxelOccupancyBuffer;
            bindings.voxelOccupancyBufferOffset = context.resources.voxelOccupancyBufferOffset_;
            bindings.voxelTrianglesListBuffer = context.resources.voxelTrianglesListBuffer;
            bindings.voxelTrianglesListBufferOffset = context.resources.voxelTrianglesListBufferOffset_;
            bindings.voxelOffsetsBuffer = context.resources.voxelOffsetsBuffer;
            bindings.voxelOffsetsBufferOffset = context.resources.voxelOffsetsBufferOffset_;
            bindings.neighborIndicesBuffer = context.resources.neighborIndicesBuffer;
            bindings.neighborIndicesBufferOffset = context.resources.neighborIndicesBufferOffset_;
            bindings.interfaceAreasBuffer = context.resources.interfaceAreasBuffer;
            bindings.interfaceAreasBufferOffset = context.resources.interfaceAreasBufferOffset_;
            bindings.interfaceNeighborIdsBuffer = context.resources.interfaceNeighborIdsBuffer;
            bindings.interfaceNeighborIdsBufferOffset = context.resources.interfaceNeighborIdsBufferOffset_;
            bindings.debugCellGeometryBuffer = context.resources.debugCellGeometryBuffer;
            bindings.debugCellGeometryBufferOffset = context.resources.debugCellGeometryBufferOffset_;
            bindings.seedFlagsBuffer = context.resources.seedFlagsBuffer;
            bindings.seedFlagsBufferOffset = context.resources.seedFlagsBufferOffset_;
            bindings.voronoiDumpBuffer = context.resources.voronoiDumpBuffer;
            bindings.voronoiDumpBufferOffset = context.resources.voronoiDumpBufferOffset_;

            voronoiGeoCompute->updateDescriptors(bindings);
            VoronoiGeoCompute::PushConstants geoPushConstants{};
            geoPushConstants.debugEnable = debugEnable ? 1u : 0u;
            geoPushConstants.nodeOffset = domain.nodeOffset;
            geoPushConstants.nodeCount = domain.nodeCount;
            voronoiGeoCompute->dispatch(geoPushConstants);
        }
    }

    freeBuffer(context.resources.meshTriangleBuffer, context.resources.meshTriangleBufferOffset_);
    freeBuffer(context.resources.voxelGridParamsBuffer, context.resources.voxelGridParamsBufferOffset_);
    freeBuffer(context.resources.voxelOccupancyBuffer, context.resources.voxelOccupancyBufferOffset_);
    freeBuffer(context.resources.voxelTrianglesListBuffer, context.resources.voxelTrianglesListBufferOffset_);
    freeBuffer(context.resources.voxelOffsetsBuffer, context.resources.voxelOffsetsBufferOffset_);

    if (debugEnable) {
        std::ofstream seedMapFile("cell_seed_positions.txt");
        seedMapFile << "# Cell Index -> Seed Position\n";
        seedMapFile << "# Seed positions (cells.size() = " << globalSeedPositions.size() << ")\n";
        for (size_t i = 0; i < globalSeedPositions.size(); ++i) {
            const auto& pos = globalSeedPositions[i];
            seedMapFile << "Cell " << i << " -> Seed at (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        }
        seedMapFile.close();
        std::cout << "[HeatSystem] Exported seed mapping to cell_seed_positions.txt" << std::endl;
    }

    if (!buildVoronoiNeighborBuffer(maxNeighbors)) {
        std::cerr << "[HeatSystem] Failed to build Voronoi neighbor buffer" << std::endl;
        return false;
    }

    VkDeviceSize tempBufferSize = sizeof(float) * context.resources.voronoiNodeCount;
    void* mappedPtr = nullptr;

    if (createStorageBuffer(
            context.memoryAllocator,
            context.vulkanDevice,
            nullptr,
            tempBufferSize,
            context.resources.tempBufferA,
            context.resources.tempBufferAOffset_,
            &mappedPtr) != VK_SUCCESS ||
        context.resources.tempBufferA == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        std::cerr << "[HeatSystem] Failed to create tempBufferA" << std::endl;
        return false;
    }
    context.resources.mappedTempBufferA = mappedPtr;

    if (createStorageBuffer(
            context.memoryAllocator,
            context.vulkanDevice,
            nullptr,
            tempBufferSize,
            context.resources.tempBufferB,
            context.resources.tempBufferBOffset_,
            &mappedPtr) != VK_SUCCESS ||
        context.resources.tempBufferB == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        std::cerr << "[HeatSystem] Failed to create tempBufferB" << std::endl;
        return false;
    }
    context.resources.mappedTempBufferB = mappedPtr;

    VkDeviceSize injectionBufferSize = sizeof(uint32_t) * context.resources.voronoiNodeCount;
    if (createStorageBuffer(
            context.memoryAllocator,
            context.vulkanDevice,
            nullptr,
            injectionBufferSize,
            context.resources.injectionKBuffer,
            context.resources.injectionKBufferOffset_,
            &mappedPtr) != VK_SUCCESS ||
        context.resources.injectionKBuffer == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        std::cerr << "[HeatSystem] Failed to create injectionKBuffer" << std::endl;
        return false;
    }
    context.resources.mappedInjectionKBuffer = mappedPtr;

    if (createStorageBuffer(
            context.memoryAllocator,
            context.vulkanDevice,
            nullptr,
            injectionBufferSize,
            context.resources.injectionKTBuffer,
            context.resources.injectionKTBufferOffset_,
            &mappedPtr) != VK_SUCCESS ||
        context.resources.injectionKTBuffer == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        std::cerr << "[HeatSystem] Failed to create injectionKTBuffer" << std::endl;
        return false;
    }
    context.resources.mappedInjectionKTBuffer = mappedPtr;

    if (context.resources.mappedInjectionKBuffer && context.resources.mappedInjectionKTBuffer) {
        std::memset(context.resources.mappedInjectionKBuffer, 0, static_cast<size_t>(injectionBufferSize));
        std::memset(context.resources.mappedInjectionKTBuffer, 0, static_cast<size_t>(injectionBufferSize));
    }

    initializeVoronoi();

    if (!createDescriptorPool(maxFramesInFlight) ||
        !createDescriptorSetLayout() ||
        !createPipeline() ||
        !createDescriptorSets(maxFramesInFlight)) {
        std::cerr << "[HeatSystem] Failed to create Voronoi descriptor/pipeline resources" << std::endl;
        return false;
    }

    uploadOccupancyPoints(receiverVoronoiDomains, pointRenderer);
    std::cout << "[HeatSystem] Geometry precomputation complete" << std::endl;
    return true;
}

void HeatSystemVoronoiStage::dispatchDiffusionSubstep(VkCommandBuffer commandBuffer, uint32_t currentFrame,
    const HeatSourcePushConstant& basePushConstant, int substepIndex, uint32_t workGroupCount) const {
    const bool isEven = (substepIndex % 2 == 0);
    VkDescriptorSet voronoiSet = context.resources.voronoiDescriptorSetsB[currentFrame];
    if (isEven) {
        voronoiSet = context.resources.voronoiDescriptorSets[currentFrame];
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.resources.voronoiPipeline);
    vkCmdPushConstants(commandBuffer, context.resources.voronoiPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HeatSourcePushConstant), &basePushConstant);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.resources.voronoiPipelineLayout, 0, 1, &voronoiSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
}

void HeatSystemVoronoiStage::insertInterSubstepBarrier(VkCommandBuffer commandBuffer, int substepIndex, uint32_t numSubsteps) const {
    if (substepIndex >= (static_cast<int>(numSubsteps) - 1)) {
        return;
    }

    const bool isEven = (substepIndex % 2 == 0);
    VkBuffer writeBuffer = context.resources.tempBufferA;
    VkDeviceSize writeOffset = context.resources.tempBufferAOffset_;
    if (isEven) {
        writeBuffer = context.resources.tempBufferB;
        writeOffset = context.resources.tempBufferBOffset_;
    }

    VkBufferMemoryBarrier barriers[3]{};

    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].buffer = writeBuffer;
    barriers[0].offset = writeOffset;
    barriers[0].size = VK_WHOLE_SIZE;

    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].buffer = context.resources.injectionKBuffer;
    barriers[1].offset = context.resources.injectionKBufferOffset_;
    barriers[1].size = VK_WHOLE_SIZE;

    barriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[2].buffer = context.resources.injectionKTBuffer;
    barriers[2].offset = context.resources.injectionKTBufferOffset_;
    barriers[2].size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        0, nullptr, 3, barriers, 0, nullptr);
}

void HeatSystemVoronoiStage::insertFinalTemperatureBarrier(VkCommandBuffer commandBuffer, uint32_t numSubsteps) const {
    const bool writesBufferB = finalSubstepWritesBufferB(numSubsteps);
    VkBuffer finalTempBuffer = context.resources.tempBufferA;
    VkDeviceSize finalTempOffset = context.resources.tempBufferAOffset_;
    if (writesBufferB) {
        finalTempBuffer = context.resources.tempBufferB;
        finalTempOffset = context.resources.tempBufferBOffset_;
    }

    VkBufferMemoryBarrier finalTempBarrier{};
    finalTempBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    finalTempBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalTempBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalTempBarrier.buffer = finalTempBuffer;
    finalTempBarrier.offset = finalTempOffset;
    finalTempBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &finalTempBarrier,
        0, nullptr);
}

bool HeatSystemVoronoiStage::finalSubstepWritesBufferB(uint32_t numSubsteps) const {
    if (numSubsteps == 0) {
        return false;
    }
    return ((numSubsteps - 1) % 2 == 0);
}

bool HeatSystemVoronoiStage::createDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 2 * 7;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * 2;

    if (vkCreateDescriptorPool(context.vulkanDevice.getDevice(), &poolInfo, nullptr,
        &context.resources.voronoiDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create Voronoi descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemVoronoiStage::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);

    bindingFlags.bindingCount = static_cast<uint32_t>(flags.size());
    bindingFlags.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    layoutInfo.pNext = &bindingFlags;

    if (vkCreateDescriptorSetLayout(context.vulkanDevice.getDevice(), &layoutInfo, nullptr,
        &context.resources.voronoiDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create Voronoi descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemVoronoiStage::createDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight * 2, context.resources.voronoiDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = context.resources.voronoiDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight * 2;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> allSets(maxFramesInFlight * 2);
    if (vkAllocateDescriptorSets(context.vulkanDevice.getDevice(), &allocInfo, allSets.data()) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to allocate Voronoi descriptor sets" << std::endl;
        return false;
    }

    context.resources.voronoiDescriptorSets.resize(maxFramesInFlight);
    context.resources.voronoiDescriptorSetsB.resize(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        context.resources.voronoiDescriptorSets[i] = allSets[i * 2];
        context.resources.voronoiDescriptorSetsB[i] = allSets[i * 2 + 1];
    }

    const uint32_t nodeCount = context.resources.voronoiNodeCount;
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        {
            std::vector<VkDescriptorBufferInfo> bufferInfos = {
                VkDescriptorBufferInfo{context.resources.voronoiNodeBuffer, context.resources.voronoiNodeBufferOffset_, sizeof(VoronoiNodeGPU) * nodeCount},
                VkDescriptorBufferInfo{context.resources.voronoiNeighborBuffer, context.resources.voronoiNeighborBufferOffset_, VK_WHOLE_SIZE},
                VkDescriptorBufferInfo{context.resources.timeBuffer, context.resources.timeBufferOffset_, sizeof(TimeUniform)},
                VkDescriptorBufferInfo{context.resources.tempBufferA, context.resources.tempBufferAOffset_, sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{context.resources.tempBufferB, context.resources.tempBufferBOffset_, sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{context.resources.seedFlagsBuffer, context.resources.seedFlagsBufferOffset_, sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{context.resources.injectionKBuffer, context.resources.injectionKBufferOffset_, sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{context.resources.injectionKTBuffer, context.resources.injectionKTBufferOffset_, sizeof(uint32_t) * nodeCount},
            };

            std::vector<VkWriteDescriptorSet> descriptorWrites(8);
            for (int j = 0; j < 8; j++) {
                descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[j].dstSet = context.resources.voronoiDescriptorSets[i];
                descriptorWrites[j].dstBinding = j;
                descriptorWrites[j].descriptorCount = 1;
                descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                if (j == 2) {
                    descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                }
                descriptorWrites[j].pBufferInfo = &bufferInfos[j];
            }
            vkUpdateDescriptorSets(context.vulkanDevice.getDevice(), 8, descriptorWrites.data(), 0, nullptr);
        }

        {
            std::vector<VkDescriptorBufferInfo> bufferInfos = {
                VkDescriptorBufferInfo{context.resources.voronoiNodeBuffer, context.resources.voronoiNodeBufferOffset_, sizeof(VoronoiNodeGPU) * nodeCount},
                VkDescriptorBufferInfo{context.resources.voronoiNeighborBuffer, context.resources.voronoiNeighborBufferOffset_, VK_WHOLE_SIZE},
                VkDescriptorBufferInfo{context.resources.timeBuffer, context.resources.timeBufferOffset_, sizeof(TimeUniform)},
                VkDescriptorBufferInfo{context.resources.tempBufferB, context.resources.tempBufferBOffset_, sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{context.resources.tempBufferA, context.resources.tempBufferAOffset_, sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{context.resources.seedFlagsBuffer, context.resources.seedFlagsBufferOffset_, sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{context.resources.injectionKBuffer, context.resources.injectionKBufferOffset_, sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{context.resources.injectionKTBuffer, context.resources.injectionKTBufferOffset_, sizeof(uint32_t) * nodeCount},
            };

            std::vector<VkWriteDescriptorSet> descriptorWrites(8);
            for (int j = 0; j < 8; j++) {
                descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[j].dstSet = context.resources.voronoiDescriptorSetsB[i];
                descriptorWrites[j].dstBinding = j;
                descriptorWrites[j].descriptorCount = 1;
                descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                if (j == 2) {
                    descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                }
                descriptorWrites[j].pBufferInfo = &bufferInfos[j];
            }
            vkUpdateDescriptorSets(context.vulkanDevice.getDevice(), 8, descriptorWrites.data(), 0, nullptr);
        }
    }
    return true;
}

bool HeatSystemVoronoiStage::createPipeline() {
    auto computeShaderCode = readFile("shaders/heat_voronoi_comp.spv");
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(context.vulkanDevice, computeShaderCode, computeShaderModule) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create Voronoi compute shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HeatSourcePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.resources.voronoiDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr,
        &context.resources.voronoiPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create Voronoi pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = context.resources.voronoiPipelineLayout;

    if (vkCreateComputePipelines(context.vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
        &context.resources.voronoiPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(context.vulkanDevice.getDevice(), context.resources.voronoiPipelineLayout, nullptr);
        context.resources.voronoiPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create Voronoi compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
    return true;
}
