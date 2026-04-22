#include "VoronoiSystem.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
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
    ModelRegistry& resourceManager,
    uint32_t maxFramesInFlight,
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight),
      voronoiBuilder(vulkanDevice, memoryAllocator, runtime.voronoiResourcesRef()) {

    VoronoiStageContext stageContext{
        vulkanDevice,
        memoryAllocator,
        resourceManager,
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

void VoronoiSystem::setParams(float cellSize, int voxelResolution) {
    VoronoiParams params{};
    params.cellSize = cellSize;
    params.voxelResolution = voxelResolution;
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
        voronoiGeoCompute.get());

    std::cout << "[VoronoiSystem] prepareVoronoiRuntime "
              << (prepared ? "succeeded" : "failed")
              << " nodeCount=" << runtime.getVoronoiNodeCount()
              << std::endl;
    return prepared;
}

void VoronoiSystem::cleanupResources() {
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

