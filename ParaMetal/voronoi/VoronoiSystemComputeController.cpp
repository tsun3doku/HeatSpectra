#include "VoronoiSystemComputeController.hpp"

#include "voronoi/VoronoiSystem.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "hash/HashProduct.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiDomainRuntime.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

VoronoiSystemComputeController::VoronoiSystemComputeController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      commandPool(commandPool) {
}

void VoronoiSystemComputeController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }
    auto it = systemsBySocket.find(socketKey);
    if (it == systemsBySocket.end()) {
        it = systemsBySocket.emplace(
            socketKey,
            std::make_unique<VoronoiSystem>(vulkanDevice, memoryAllocator, commandPool)).first;
    }

    auto& system = it->second;
    if (!system) {
        return;
    }

    const auto configIt = configuredConfigs.find(socketKey);
    if (configIt != configuredConfigs.end() && configIt->second.computeHash == config.computeHash) {
        return;
    }
    const auto failedIt = failedComputeHashes.find(socketKey);
    if (failedIt != failedComputeHashes.end() && failedIt->second == config.computeHash) {
        return;
    }
    failedComputeHashes.erase(socketKey);

    system->setParams(config.cellSize, config.voxelResolution);
    if (config.isPointDomain) {
        system->clearGeometry();
        system->setPointGeometry(config.pointPositions, config.pointDomainCorners);
    } else {
        system->setMeshGeometry(
            config.geometryPositions,
            config.geometryTriangleIndices,
            config.surfaceVertices,
            config.surfaceTriangleIndices,
            config.runtimeModelId,
            config.meshModelMatrix);
        system->setSeedPositions(config.pointPositions, config.pointDomainCorners);
    }
    const bool configured = system->ensureConfigured();

    if (configured) {
        configuredConfigs[socketKey] = config;
        failedComputeHashes.erase(socketKey);
    } else {
        configuredConfigs.erase(socketKey);
        failedComputeHashes[socketKey] = config.computeHash;
    }
}

void VoronoiSystemComputeController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs.erase(socketKey);
    failedComputeHashes.erase(socketKey);
    auto it = systemsBySocket.find(socketKey);
    if (it != systemsBySocket.end()) {
        if (it->second) {
            vkDeviceWaitIdle(vulkanDevice.getDevice());
            it->second->cleanupResources();
            it->second->cleanup();
        }
        systemsBySocket.erase(it);
    }
}

void VoronoiSystemComputeController::disableAll() {
    configuredConfigs.clear();
    failedComputeHashes.clear();
    if (!systemsBySocket.empty()) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

    for (auto& [key, system] : systemsBySocket) {
        (void)key;
        if (system) {
            system->cleanupResources();
            system->cleanup();
        }
    }
    systemsBySocket.clear();
}

std::vector<VoronoiSystem*> VoronoiSystemComputeController::getActiveSystems() const {
    std::vector<VoronoiSystem*> systems;
    systems.reserve(systemsBySocket.size());
    for (const auto& [key, system] : systemsBySocket) {
        (void)key;
        if (system && system->isReady()) {
            systems.push_back(system.get());
        }
    }
    return systems;
}


const VoronoiSystem* VoronoiSystemComputeController::getSystem(uint64_t socketKey) const {
    const auto it = systemsBySocket.find(socketKey);
    if (it == systemsBySocket.end() || !it->second || !it->second->isReady()) {
        return nullptr;
    }
    return it->second.get();
}

bool VoronoiSystemComputeController::buildProduct(uint64_t socketKey, VoronoiProduct& outProduct) const {
    outProduct = {};
    const VoronoiSystem* voronoiSystem = getSystem(socketKey);
    if (!voronoiSystem) {
        return false;
    }
    if (!getConfig(socketKey)) {
        return false;
    }

    const VoronoiNodeDomain& nodeDomain = voronoiSystem->runtimeRef().getNodeDomain();
    outProduct.candidateNodeCount = voronoiSystem->getCandidateNodeCount();
    outProduct.nodeCount = nodeDomain.getNodeCount();
    outProduct.couplingCount = voronoiSystem->getBuildStage().getCouplingCount();
    outProduct.nodes = nodeDomain.getNodes();
    outProduct.couplings = nodeDomain.getCouplings();
    outProduct.surfacePatchAreas = nodeDomain.getSurfacePatchAreas();
    outProduct.nodePositions = nodeDomain.getNodeIndex().getNodePositions();
    outProduct.surfaceNodeIds = nodeDomain.getSurfaceNodeIds();
    outProduct.surfaceStencils = nodeDomain.getSurfaceStencils();
    outProduct.surfaceValueWeights = nodeDomain.getSurfaceValueWeights();
    outProduct.surfaceGradientWeights = nodeDomain.getSurfaceGradientWeights();

    const VoronoiSystemBuildStage& buildStage = voronoiSystem->getBuildStage();
    outProduct.candidateNodeBuffer = buildStage.getCandidateNodeBuffer();
    outProduct.candidateNodeBufferOffset = buildStage.getCandidateNodeBufferOffset();
    outProduct.candidateNeighborIndicesBuffer = buildStage.getCandidateNeighborIndicesBuffer();
    outProduct.candidateNeighborIndicesBufferOffset = buildStage.getCandidateNeighborIndicesBufferOffset();
    outProduct.nodeBuffer = buildStage.getNodeBuffer();
    outProduct.nodeBufferOffset = buildStage.getNodeBufferOffset();
    outProduct.couplingBuffer = buildStage.getCouplingBuffer();
    outProduct.couplingBufferOffset = buildStage.getCouplingBufferOffset();
    outProduct.seedPositionBuffer = buildStage.getSeedPositionBuffer();
    outProduct.seedPositionBufferOffset = buildStage.getSeedPositionBufferOffset();
    outProduct.occupancyPointBuffer = buildStage.getOccupancyPointBuffer();
    outProduct.occupancyPointBufferOffset = buildStage.getOccupancyPointBufferOffset();
    outProduct.occupancyPointCount = buildStage.getOccupancyPointCount();

    const VoronoiDomainRuntime* domainRuntime = voronoiSystem->getDomainRuntime();
    if (!domainRuntime) {
        return false;
    }
    outProduct.isPointDomain = domainRuntime->isPointDomain();
    outProduct.candidateBuffer = domainRuntime->getCandidateBuffer();
    outProduct.candidateBufferOffset = domainRuntime->getCandidateBufferOffset();

    if (!domainRuntime->isPointDomain()) {
        const VoronoiModelRuntime* modelRuntime = static_cast<const VoronoiModelRuntime*>(domainRuntime);
        outProduct.runtimeModelId = modelRuntime->getRuntimeModelId();
        outProduct.gmlsSurfaceStencilBuffer = modelRuntime->getGMLSSurfaceStencilBuffer();
        outProduct.gmlsSurfaceStencilBufferOffset = modelRuntime->getGMLSSurfaceStencilBufferOffset();
        outProduct.gmlsSurfaceWeightBuffer = modelRuntime->getGMLSSurfaceWeightBuffer();
        outProduct.gmlsSurfaceWeightBufferOffset = modelRuntime->getGMLSSurfaceWeightBufferOffset();
        outProduct.gmlsSurfaceWeightCount = modelRuntime->getGMLSSurfaceWeightCount();
        outProduct.gmlsSurfaceGradientWeightBuffer = modelRuntime->getGMLSSurfaceGradientWeightBuffer();
        outProduct.gmlsSurfaceGradientWeightBufferOffset = modelRuntime->getGMLSSurfaceGradientWeightBufferOffset();
        outProduct.gmlsSurfaceGradientWeightCount = modelRuntime->getGMLSSurfaceGradientWeightCount();
    } else {
        outProduct.runtimeModelId = 0;
    }

    HashProduct::seal(outProduct);
    return outProduct.isValid();
}

const VoronoiSystemComputeController::Config* VoronoiSystemComputeController::getConfig(uint64_t socketKey) const {
    const auto it = configuredConfigs.find(socketKey);
    if (it == configuredConfigs.end()) return nullptr;
    return &it->second;
}
