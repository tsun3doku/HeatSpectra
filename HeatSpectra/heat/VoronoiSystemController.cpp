#include "VoronoiSystemController.hpp"

#include "VoronoiSystem.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

VoronoiSystemController::VoronoiSystemController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ModelRegistry& resourceManager,
    UniformBufferManager& uniformBufferManager,
    CommandPool& renderCommandPool,
    uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight) {
}

std::unique_ptr<VoronoiSystem> VoronoiSystemController::buildVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass) {
    auto system = std::make_unique<VoronoiSystem>(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        uniformBufferManager,
        maxFramesInFlight,
        renderCommandPool,
        extent,
        renderPass);
    if (!system || !system->isInitialized()) {
        return nullptr;
    }

    return system;
}

void VoronoiSystemController::createVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void VoronoiSystemController::updateRenderContext(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void VoronoiSystemController::updateRenderResources() {
    for (auto& [socketKey, system] : voronoiSystems) {
        if (system) {
            system->updateRenderResources(currentRenderPass);
        }
    }
}

void VoronoiSystemController::configure(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs[socketKey] = config;
    auto& system = voronoiSystems[socketKey];
    if (!system && currentRenderPass != VK_NULL_HANDLE) {
        system = buildVoronoiSystem(currentExtent, currentRenderPass);
    }

    if (system) {
        system->setReceiverPayloads(
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
        system->setParams(config.params);
        system->ensureConfigured();
    }
}

void VoronoiSystemController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs.erase(socketKey);
    auto it = voronoiSystems.find(socketKey);
    if (it != voronoiSystems.end()) {
        if (it->second) {
            it->second->cleanupResources();
            it->second->cleanup();
        }
        voronoiSystems.erase(it);
    }
}

void VoronoiSystemController::disableAll() {
    configuredConfigs.clear();
    for (auto& [key, system] : voronoiSystems) {
        if (system) {
            system->cleanupResources();
            system->cleanup();
        }
    }
    voronoiSystems.clear();
}

VoronoiSystem* VoronoiSystemController::getVoronoiSystem(uint64_t socketKey) const {
    const auto it = voronoiSystems.find(socketKey);
    if (it != voronoiSystems.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<VoronoiSystem*> VoronoiSystemController::getActiveSystems() const {
    std::vector<VoronoiSystem*> activeSystems;
    activeSystems.reserve(voronoiSystems.size());
    for (const auto& [key, system] : voronoiSystems) {
        if (system && system->isReady()) {
            activeSystems.push_back(system.get());
        }
    }
    return activeSystems;
}

bool VoronoiSystemController::exportProduct(uint64_t socketKey, VoronoiProduct& outProduct) const {
    outProduct = {};

    const auto it = voronoiSystems.find(socketKey);
    if (it == voronoiSystems.end() || !it->second || !it->second->isReady()) {
        return false;
    }

    const auto& voronoiSystem = it->second;

    outProduct.nodeCount = voronoiSystem->getVoronoiNodeCount();

    const VoronoiResources& resources = voronoiSystem->voronoiResourcesRef();
    outProduct.mappedVoronoiNodes = static_cast<const VoronoiNode*>(resources.mappedVoronoiNodeData);
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
    outProduct.seedFlagsBuffer = resources.seedFlagsBuffer;
    outProduct.seedFlagsBufferOffset = resources.seedFlagsBufferOffset;

    const auto& modelRuntimes = voronoiSystem->getModelRuntimes();
    const auto& receiverDomains = voronoiSystem->getReceiverVoronoiDomains();
    outProduct.receiverProducts.reserve(receiverDomains.size());
    for (const VoronoiDomain& domain : receiverDomains) {
        VoronoiReceiverProduct receiverProduct{};
        receiverProduct.runtimeModelId = domain.receiverModelId;
        receiverProduct.nodeOffset = domain.nodeOffset;
        receiverProduct.nodeCount = domain.nodeCount;
        receiverProduct.seedFlags = domain.seedFlags;

        for (const auto& modelRuntime : modelRuntimes) {
            if (!modelRuntime || modelRuntime->getRuntimeModelId() != domain.receiverModelId) {
                continue;
            }

            receiverProduct.surfaceMappingBuffer = modelRuntime->getVoronoiMappingBuffer();
            receiverProduct.surfaceMappingBufferOffset = modelRuntime->getVoronoiMappingBufferOffset();
            receiverProduct.surfaceCellIndices = modelRuntime->getVoronoiSurfaceCellIndices();
            break;
        }

        outProduct.receiverProducts.push_back(std::move(receiverProduct));
    }

    outProduct.contentHash = computeContentHash(outProduct);

    return outProduct.isValid();
}

