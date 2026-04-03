#include "VoronoiSystemController.hpp"

#include "VoronoiSystem.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

VoronoiSystemController::VoronoiSystemController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ResourceManager& resourceManager,
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
    voronoiSystem = buildVoronoiSystem(extent, renderPass);
}

void VoronoiSystemController::recreateVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass) {
    if (voronoiSystem) {
        voronoiSystem->cleanupResources();
        voronoiSystem->cleanup();
        voronoiSystem.reset();
    }

    voronoiSystem = buildVoronoiSystem(extent, renderPass);
}

void VoronoiSystemController::configure(const Config& config) {
    configuredConfig = config;
    if (!voronoiSystem) {
        return;
    }

    voronoiSystem->setReceiverPayloads(
        configuredConfig.receiverNodeModelIds,
        configuredConfig.receiverGeometryPositions,
        configuredConfig.receiverGeometryTriangleIndices,
        configuredConfig.receiverIntrinsicMeshes,
        configuredConfig.receiverSurfaceVertices,
        configuredConfig.receiverIntrinsicTriangleIndices,
        configuredConfig.receiverRuntimeModelIds,
        configuredConfig.meshVertexBuffers,
        configuredConfig.meshVertexBufferOffsets,
        configuredConfig.meshIndexBuffers,
        configuredConfig.meshIndexBufferOffsets,
        configuredConfig.meshIndexCounts,
        configuredConfig.meshModelMatrices,
        configuredConfig.supportingHalfedgeViews,
        configuredConfig.supportingAngleViews,
        configuredConfig.halfedgeViews,
        configuredConfig.edgeViews,
        configuredConfig.triangleViews,
        configuredConfig.lengthViews,
        configuredConfig.inputHalfedgeViews,
        configuredConfig.inputEdgeViews,
        configuredConfig.inputTriangleViews,
        configuredConfig.inputLengthViews);
    voronoiSystem->setParams(configuredConfig.params);
    voronoiSystem->ensureConfigured();
}

void VoronoiSystemController::disable() {
    configuredConfig = {};
    if (!voronoiSystem) {
        return;
    }

    voronoiSystem->clearReceiverPayloads();
}

VoronoiSystem* VoronoiSystemController::getVoronoiSystem() const {
    return voronoiSystem.get();
}

bool VoronoiSystemController::exportProduct(VoronoiProduct& outProduct) const {
    outProduct = {};

    if (!voronoiSystem || !voronoiSystem->isReady()) {
        return false;
    }

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

    return outProduct.isValid();
}
