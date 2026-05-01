#include "VoronoiSystemComputeController.hpp"

#include <iostream>
#include "VoronoiSystem.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

VoronoiSystemComputeController::VoronoiSystemComputeController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ModelRegistry& resourceManager,
    CommandPool& renderCommandPool,
    uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight) {
}

std::unique_ptr<VoronoiSystem> VoronoiSystemComputeController::buildVoronoiSystem() {
    auto system = std::make_unique<VoronoiSystem>(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        maxFramesInFlight,
        renderCommandPool);
    if (!system || !system->isInitialized()) {
        return nullptr;
    }

    return system;
}

void VoronoiSystemComputeController::configure(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    auto& system = activeSystems[socketKey];
    if (!system) {
        system = buildVoronoiSystem();
    }

    const auto configIt = configuredConfigs.find(socketKey);
    if (configIt != configuredConfigs.end() && configIt->second.computeHash == config.computeHash) {
        return;
    }

    configuredConfigs[socketKey] = config;

    if (system) {
        system->setReceiverGeometry(
            config.receiverNodeModelIds,
            config.receiverGeometryPositions,
            config.receiverGeometryTriangleIndices,
            config.receiverIntrinsicMeshes,
            config.receiverSurfaceVertices,
            config.receiverIntrinsicTriangleIndices,
            config.receiverRuntimeModelIds,
            config.meshVertexBuffers,
            config.meshVertexBufferOffsets,
            config.meshIndexBuffers,
            config.meshIndexBufferOffsets,
            config.meshIndexCounts,
            config.meshModelMatrices,
            config.supportingHalfedgeViews,
            config.supportingAngleViews,
            config.halfedgeViews,
            config.edgeViews,
            config.triangleViews,
            config.lengthViews,
            config.inputHalfedgeViews,
            config.inputEdgeViews,
            config.inputTriangleViews,
            config.inputLengthViews);
        system->setParams(config.cellSize, config.voxelResolution);
        system->ensureConfigured();
    }
}

void VoronoiSystemComputeController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs.erase(socketKey);
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second) {
            it->second->cleanupResources();
            it->second->cleanup();
        }
        activeSystems.erase(it);
    }
}

void VoronoiSystemComputeController::disableAll() {
    configuredConfigs.clear();
    for (auto& [key, system] : activeSystems) {
        (void)key;
        if (system) {
            system->cleanupResources();
            system->cleanup();
        }
    }
    activeSystems.clear();
}

std::vector<VoronoiSystem*> VoronoiSystemComputeController::getActiveSystems() const {
    std::vector<VoronoiSystem*> systems;
    systems.reserve(activeSystems.size());
    for (const auto& [key, system] : activeSystems) {
        (void)key;
        if (system && system->isReady()) {
            systems.push_back(system.get());
        }
    }
    return systems;
}

bool VoronoiSystemComputeController::exportProduct(uint64_t socketKey, VoronoiProduct& outProduct) const {
    outProduct = {};

    if (configuredConfigs.find(socketKey) == configuredConfigs.end()) {
        return false;
    }

    const auto it = activeSystems.find(socketKey);
    if (it == activeSystems.end() || !it->second || !it->second->isReady()) {
        return false;
    }

    const auto& voronoiSystem = it->second;

    outProduct.nodeCount = voronoiSystem->getVoronoiNodeCount();

    const VoronoiResources& resources = voronoiSystem->resourcesRef();
    outProduct.mappedVoronoiNodes = static_cast<const voronoi::Node*>(resources.mappedVoronoiNodeData);
    outProduct.nodeBuffer = resources.voronoiNodeBuffer;
    outProduct.nodeBufferOffset = resources.voronoiNodeBufferOffset;
    outProduct.voronoiNeighborBuffer = resources.voronoiNeighborBuffer;
    outProduct.voronoiNeighborBufferOffset = resources.voronoiNeighborBufferOffset;
    outProduct.neighborIndicesBuffer = resources.neighborIndicesBuffer;
    outProduct.neighborIndicesBufferOffset = resources.neighborIndicesBufferOffset;
    outProduct.interfaceAreasBuffer = resources.interfaceAreasBuffer;
    outProduct.interfaceAreasBufferOffset = resources.interfaceAreasBufferOffset;
    outProduct.interfaceNeighborIdsBuffer = resources.interfaceNeighborIdsBuffer;
    outProduct.interfaceNeighborIdsBufferOffset = resources.interfaceNeighborIdsBufferOffset;
    outProduct.gmlsInterfaceBuffer = resources.gmlsInterfaceBuffer;
    outProduct.gmlsInterfaceBufferOffset = resources.gmlsInterfaceBufferOffset;
    outProduct.seedFlagsBuffer = resources.seedFlagsBuffer;
    outProduct.seedFlagsBufferOffset = resources.seedFlagsBufferOffset;
    outProduct.seedPositionBuffer = resources.seedPositionBuffer;
    outProduct.seedPositionBufferOffset = resources.seedPositionBufferOffset;
    outProduct.occupancyPointBuffer = resources.occupancyPointBuffer;
    outProduct.occupancyPointBufferOffset = resources.occupancyPointBufferOffset;
    outProduct.occupancyPointCount = resources.occupancyPointCount;

    const auto& modelRuntimes = voronoiSystem->getModelRuntimes();
    const auto& receiverDomains = voronoiSystem->getReceiverVoronoiDomains();
    outProduct.surfaces.reserve(receiverDomains.size());
    for (const VoronoiDomain& domain : receiverDomains) {
        VoronoiSurfaceProduct surfaceProduct{};
        surfaceProduct.runtimeModelId = domain.receiverModelId;
        surfaceProduct.nodeOffset = domain.nodeOffset;
        surfaceProduct.nodeCount = domain.nodeCount;
        surfaceProduct.seedFlags = domain.seedFlags;
        const auto& domainSeedPositions = domain.integrator->getSeedPositions();
        surfaceProduct.seedPositions.reserve(domainSeedPositions.size());
        for (const glm::vec4& seedPosition : domainSeedPositions) {
            surfaceProduct.seedPositions.push_back(glm::vec3(seedPosition));
        }

        for (const auto& modelRuntime : modelRuntimes) {
            if (!modelRuntime || modelRuntime->getRuntimeModelId() != domain.receiverModelId) {
                continue;
            }

            surfaceProduct.vertexBuffer = modelRuntime->getVertexBuffer();
            surfaceProduct.vertexBufferOffset = modelRuntime->getVertexBufferOffset();
            surfaceProduct.indexBuffer = modelRuntime->getIndexBuffer();
            surfaceProduct.indexBufferOffset = modelRuntime->getIndexBufferOffset();
            surfaceProduct.indexCount = modelRuntime->getIndexCount();
            surfaceProduct.modelMatrix = modelRuntime->getModelMatrix();
            surfaceProduct.intrinsicVertexCount = static_cast<uint32_t>(modelRuntime->getIntrinsicVertexCount());
            surfaceProduct.candidateBuffer = modelRuntime->getVoronoiCandidateBuffer();
            surfaceProduct.candidateBufferOffset = modelRuntime->getVoronoiCandidateBufferOffset();
            surfaceProduct.gmlsSurfaceStencilBuffer = modelRuntime->getGMLSSurfaceStencilBuffer();
            surfaceProduct.gmlsSurfaceStencilBufferOffset = modelRuntime->getGMLSSurfaceStencilBufferOffset();
            surfaceProduct.gmlsSurfaceWeightBuffer = modelRuntime->getGMLSSurfaceWeightBuffer();
            surfaceProduct.gmlsSurfaceWeightBufferOffset = modelRuntime->getGMLSSurfaceWeightBufferOffset();
            surfaceProduct.gmlsSurfaceGradientWeightBuffer = modelRuntime->getGMLSSurfaceGradientWeightBuffer();
            surfaceProduct.gmlsSurfaceGradientWeightBufferOffset = modelRuntime->getGMLSSurfaceGradientWeightBufferOffset();
            surfaceProduct.supportingHalfedgeView = modelRuntime->getSupportingHalfedgeView();
            surfaceProduct.supportingAngleView = modelRuntime->getSupportingAngleView();
            surfaceProduct.halfedgeView = modelRuntime->getHalfedgeView();
            surfaceProduct.edgeView = modelRuntime->getEdgeView();
            surfaceProduct.triangleView = modelRuntime->getTriangleView();
            surfaceProduct.lengthView = modelRuntime->getLengthView();
            surfaceProduct.inputHalfedgeView = modelRuntime->getInputHalfedgeView();
            surfaceProduct.inputEdgeView = modelRuntime->getInputEdgeView();
            surfaceProduct.inputTriangleView = modelRuntime->getInputTriangleView();
            surfaceProduct.inputLengthView = modelRuntime->getInputLengthView();
            break;
        }

        outProduct.surfaces.push_back(std::move(surfaceProduct));
    }

    outProduct.productHash = buildProductHash(outProduct);

    return outProduct.isValid();
}
