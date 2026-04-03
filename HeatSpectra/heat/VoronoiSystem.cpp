#include "VoronoiSystem.hpp"

#include "renderers/PointRenderer.hpp"
#include "renderers/VoronoiRenderer.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiCandidateCompute.hpp"
#include "voronoi/VoronoiSurfaceStage.hpp"
#include "voronoi/VoronoiStageContext.hpp"
#include "voronoi/VoronoiGeoCompute.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"
#include "voronoi/VoronoiSurfaceRuntime.hpp"

#include <glm/mat4x4.hpp>
#include <iostream>

VoronoiSystem::VoronoiSystem(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ResourceManager& resourceManager,
    UniformBufferManager& uniformBufferManager,
    uint32_t maxFramesInFlight,
    CommandPool& renderCommandPool,
    VkExtent2D extent,
    VkRenderPass renderPass)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight),
      voronoiBuilder(vulkanDevice, memoryAllocator, runtime.voronoiResourcesRef()) {
    (void)extent;

    VoronoiStageContext stageContext{
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        uniformBufferManager,
        renderCommandPool,
        runtime.resourcesRef()
    };

    surfaceStage = std::make_unique<VoronoiSurfaceStage>(stageContext);

    if (!createSurfaceDescriptorPool(maxFramesInFlight) ||
        !createSurfaceDescriptorSetLayout() ||
        !createSurfacePipeline()) {
        failInitialization("create surface compute resources");
        return;
    }

    initializeVoronoiRenderer(renderPass, maxFramesInFlight);
    initializePointRenderer(renderPass, maxFramesInFlight);
    initializeVoronoiGeoCompute();
    initializeVoronoiCandidateCompute();

    initialized = true;
}

VoronoiSystem::~VoronoiSystem() {
}

void VoronoiSystem::failInitialization(const char* stage) {
    std::cerr << "[VoronoiSystem] Initialization failed at stage: " << stage << std::endl;
    cleanupResources();
    cleanup();
}

void VoronoiSystem::setReceiverPayloads(
    const std::vector<uint32_t>& receiverNodeModelIds,
    const std::vector<std::vector<glm::vec3>>& receiverGeometryPositions,
    const std::vector<std::vector<uint32_t>>& receiverGeometryTriangleIndices,
    const std::vector<SupportingHalfedge::IntrinsicMesh>& receiverIntrinsicMeshes,
    const std::vector<std::vector<VoronoiGeometryRuntime::SurfaceVertex>>& receiverSurfaceVertices,
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
    runtime.setReceiverPayloads(
        vulkanDevice,
        memoryAllocator,
        renderCommandPool,
        receiverNodeModelIds,
        receiverGeometryPositions,
        receiverGeometryTriangleIndices,
        receiverIntrinsicMeshes,
        receiverSurfaceVertices,
        receiverIntrinsicTriangleIndices,
        receiverModelIds,
        meshVertexBuffers,
        meshVertexBufferOffsets,
        meshIndexBuffers,
        meshIndexBufferOffsets,
        meshIndexCounts,
        meshModelMatrices,
        supportingHalfedgeViews,
        supportingAngleViews,
        halfedgeViews,
        edgeViews,
        triangleViews,
        lengthViews,
        inputHalfedgeViews,
        inputEdgeViews,
        inputTriangleViews,
        inputLengthViews);

    surfaceRuntime.cleanup();
    surfaceRuntime.initializeGeometryBindings(
        vulkanDevice,
        memoryAllocator,
        receiverSurfaceVertices,
        receiverIntrinsicTriangleIndices,
        receiverModelIds,
        runtime.getModelRuntimes());
}

void VoronoiSystem::clearReceiverPayloads() {
    surfaceRuntime.cleanup();
    runtime.clearReceiverPayloads();
}

void VoronoiSystem::setParams(const VoronoiParams& params) {
    runtime.setParams(params);
}

bool VoronoiSystem::ensureConfigured() {
    if (runtime.isReady()) {
        runtime.executeBufferTransfers(renderCommandPool, surfaceRuntime, voronoiCandidateCompute.get());
        return true;
    }

    if (!prepareVoronoiRuntime()) {
        std::cout << "[VoronoiSystem] prepareVoronoiRuntime failed" << std::endl;
        return false;
    }

    runtime.executeBufferTransfers(renderCommandPool, surfaceRuntime, voronoiCandidateCompute.get());
    return true;
}

void VoronoiSystem::initializeVoronoiRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    voronoiRenderer = std::make_unique<VoronoiRenderer>(vulkanDevice, uniformBufferManager, renderCommandPool);
    if (voronoiRenderer) {
        voronoiRenderer->initialize(renderPass, maxFramesInFlight);
    }
}

void VoronoiSystem::initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    pointRenderer = std::make_unique<PointRenderer>(vulkanDevice, memoryAllocator, uniformBufferManager);
    if (pointRenderer) {
        pointRenderer->initialize(renderPass, 2, maxFramesInFlight);
    }
}

void VoronoiSystem::initializeVoronoiGeoCompute() {
    voronoiGeoCompute = std::make_unique<VoronoiGeoCompute>(vulkanDevice, renderCommandPool);
}

void VoronoiSystem::initializeVoronoiCandidateCompute() {
    voronoiCandidateCompute = std::make_unique<VoronoiCandidateCompute>(vulkanDevice, renderCommandPool);
    if (voronoiCandidateCompute) {
        voronoiCandidateCompute->initialize();
    }
}

bool VoronoiSystem::createSurfaceDescriptorPool(uint32_t maxFramesInFlight) {
    if (!surfaceStage) {
        return false;
    }
    return surfaceStage->createDescriptorPool(maxFramesInFlight);
}

bool VoronoiSystem::createSurfaceDescriptorSetLayout() {
    if (!surfaceStage) {
        return false;
    }
    return surfaceStage->createDescriptorSetLayout();
}

bool VoronoiSystem::createSurfacePipeline() {
    if (!surfaceStage) {
        return false;
    }
    return surfaceStage->createPipeline();
}

bool VoronoiSystem::prepareVoronoiRuntime() {
    const bool prepared = runtime.prepare(
        voronoiBuilder,
        debugEnable,
        K_NEIGHBORS,
        voronoiGeoCompute.get(),
        pointRenderer.get());

    std::cout << "[VoronoiSystem] prepareVoronoiRuntime "
              << (prepared ? "succeeded" : "failed")
              << " nodeCount=" << runtime.getVoronoiNodeCount()
              << std::endl;
    return prepared;
}

void VoronoiSystem::recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) {
    (void)extent;
    this->maxFramesInFlight = maxFramesInFlight;

    VoronoiSystemResources& resources = runtime.resourcesRef();
    if (runtime.isSeederReady()) {
        if (resources.surfaceDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), resources.surfaceDescriptorPool, 0);
        }
    }

    initializeVoronoiRenderer(renderPass, maxFramesInFlight);
    initializePointRenderer(renderPass, maxFramesInFlight);
    if (runtime.isSeederReady()) {
        if (resources.surfacePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vulkanDevice.getDevice(), resources.surfacePipeline, nullptr);
            resources.surfacePipeline = VK_NULL_HANDLE;
        }
        if (resources.surfacePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vulkanDevice.getDevice(), resources.surfacePipelineLayout, nullptr);
            resources.surfacePipelineLayout = VK_NULL_HANDLE;
        }
        if (resources.surfaceDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), resources.surfaceDescriptorSetLayout, nullptr);
            resources.surfaceDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (!createSurfaceDescriptorSetLayout() || !createSurfacePipeline()) {
            std::cerr << "[VoronoiSystem] Failed to recreate surface resources" << std::endl;
            return;
        }
    }
}

void VoronoiSystem::renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!voronoiRenderer || !runtime.isReady()) {
        return;
    }

    const VoronoiResources& resources = runtime.voronoiResourcesRef();
    const auto& modelRuntimes = runtime.getModelRuntimes();

    for (const auto& modelRuntime : modelRuntimes) {
        if (!modelRuntime) {
            continue;
        }

        const uint32_t runtimeModelId = modelRuntime->getRuntimeModelId();
        const VoronoiDomain* receiverDomain = runtime.findReceiverDomain(runtimeModelId);
        if (!receiverDomain || receiverDomain->nodeCount == 0) {
            continue;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(modelRuntime->getIntrinsicVertexCount());
        const VkBuffer candidateBuffer = modelRuntime->getVoronoiCandidateBuffer();
        if (candidateBuffer == VK_NULL_HANDLE || vertexCount == 0) {
            continue;
        }

        voronoiRenderer->updateDescriptors(
            frameIndex,
            vertexCount,
            resources.seedPositionBuffer,
            resources.seedPositionBufferOffset,
            resources.neighborIndicesBuffer,
            resources.neighborIndicesBufferOffset,
            modelRuntime->getSupportingHalfedgeView(),
            modelRuntime->getSupportingAngleView(),
            modelRuntime->getHalfedgeView(),
            modelRuntime->getEdgeView(),
            modelRuntime->getTriangleView(),
            modelRuntime->getLengthView(),
            modelRuntime->getInputHalfedgeView(),
            modelRuntime->getInputEdgeView(),
            modelRuntime->getInputTriangleView(),
            modelRuntime->getInputLengthView(),
            candidateBuffer,
            modelRuntime->getVoronoiCandidateBufferOffset());

        voronoiRenderer->render(
            cmdBuffer,
            modelRuntime->getVertexBuffer(),
            modelRuntime->getVertexBufferOffset(),
            modelRuntime->getIndexBuffer(),
            modelRuntime->getIndexBufferOffset(),
            modelRuntime->getIndexCount(),
            frameIndex,
            modelRuntime->getModelMatrix());
    }
}

void VoronoiSystem::renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (!pointRenderer) {
        return;
    }
    pointRenderer->render(cmdBuffer, frameIndex, glm::mat4(1.0f), extent);
}

void VoronoiSystem::cleanupResources() {
    if (voronoiRenderer) {
        voronoiRenderer->cleanup();
        voronoiRenderer.reset();
    }
    if (pointRenderer) {
        pointRenderer->cleanup();
        pointRenderer.reset();
    }

    runtime.cleanupResources(vulkanDevice);

    if (voronoiGeoCompute) {
        voronoiGeoCompute->cleanupResources();
    }
    if (voronoiCandidateCompute) {
        voronoiCandidateCompute->cleanupResources();
    }
}

void VoronoiSystem::cleanup() {
    surfaceRuntime.cleanup();
    runtime.cleanup(memoryAllocator);
}
