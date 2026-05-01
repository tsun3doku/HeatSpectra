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
      voronoiBuilder(vulkanDevice, memoryAllocator, runtime.resourcesRef()) {

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

void VoronoiSystem::setReceiverGeometry(
    const std::vector<uint32_t>& receiverNodeModelIds,
    const std::vector<std::vector<glm::vec3>>& receiverGeometryPositions,
    const std::vector<std::vector<uint32_t>>& receiverGeometryTriangleIndices,
    const std::vector<SupportingHalfedge::IntrinsicMesh>& receiverIntrinsicMeshes,
        const std::vector<std::vector<VoronoiModelRuntime::SurfaceVertex>>& receiverSurfaceVertices,
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
    runtime.setReceiverGeometry(
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

}

void VoronoiSystem::clearReceiverGeometry() {
    runtime.clearReceiverGeometry();
}

void VoronoiSystem::setParams(float cellSize, int voxelResolution) {
    runtime.setParams(cellSize, voxelResolution);
}

bool VoronoiSystem::ensureConfigured() {
    if (runtime.isReady()) {
        executeBufferTransfers();
        return true;
    }

    if (!rebuildVoronoiRuntime()) {
        std::cerr << "[VoronoiSystem] rebuildVoronoiRuntime failed" << std::endl;
        return false;
    }

    executeBufferTransfers();
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

bool VoronoiSystem::rebuildVoronoiRuntime() {
    std::vector<VoronoiDomain>& receiverVoronoiDomains = runtime.receiverVoronoiDomainsRef();
    if (!voronoiBuilder.buildDomains(
            runtime.getModelRuntimes(),
            receiverVoronoiDomains,
            runtime.getCellSize(),
            runtime.getVoxelResolution(),
            K_NEIGHBORS)) {
        std::cerr << "[VoronoiSystem] Failed to build Voronoi domains" << std::endl;
        return false;
    }

    voronoiBuilder.setGhost(receiverVoronoiDomains, false);

    runtime.markSeederReady();

    if (!voronoiBuilder.generateDiagram(
            receiverVoronoiDomains,
            debugEnable,
            K_NEIGHBORS,
            voronoiGeoCompute.get())) {
        std::cerr << "[VoronoiSystem] Voronoi generation failed" << std::endl;
        return false;
    }

    if (runtime.resourcesRef().voronoiNodeCount == 0) {
        std::cerr << "[VoronoiSystem] Voronoi generation produced zero nodes" << std::endl;
        return false;
    }

    if (!voronoiBuilder.stageSurfaceMappings(receiverVoronoiDomains)) {
        return false;
    }

    runtime.markReady();
    return true;
}

void VoronoiSystem::executeBufferTransfers() {
    runtime.uploadModelStagingBuffers(renderCommandPool);
    dispatchVoronoiCandidateUpdates();
}

void VoronoiSystem::dispatchVoronoiCandidateUpdates() {
    if (!voronoiCandidateCompute || runtime.resourcesRef().voronoiNodeCount == 0) {
        std::cerr << "[VoronoiSystem] Skipping Voronoi candidate updates"
                  << " compute=" << (voronoiCandidateCompute ? "present" : "missing")
                  << " nodeCount=" << runtime.resourcesRef().voronoiNodeCount
                  << std::endl;
        return;
    }

    const VoronoiResources& resources = runtime.resourcesRef();
    size_t dispatchedCount = 0;
    size_t skippedMissingDomain = 0;
    size_t skippedMissingGeometryRuntime = 0;
    size_t skippedZeroFaces = 0;
    size_t skippedMissingCandidateBuffer = 0;
    for (const auto& modelRuntime : runtime.getModelRuntimes()) {
        const uint32_t receiverModelId = modelRuntime->getRuntimeModelId();
        const VoronoiDomain* receiverDomain = runtime.findReceiverDomain(receiverModelId);
        if (!receiverDomain || receiverDomain->nodeCount == 0) {
            ++skippedMissingDomain;
            continue;
        }

        uint32_t faceCount = static_cast<uint32_t>(modelRuntime->getIntrinsicTriangleCount());
        if (modelRuntime->getSurfaceBuffer() == VK_NULL_HANDLE) {
            ++skippedMissingGeometryRuntime;
            continue;
        }
        if (faceCount == 0) {
            ++skippedZeroFaces;
            continue;
        }
        if (modelRuntime->getVoronoiCandidateBuffer() == VK_NULL_HANDLE) {
            ++skippedMissingCandidateBuffer;
            continue;
        }

        VoronoiCandidateCompute::Bindings bindings{};
        bindings.vertexBuffer = modelRuntime->getSurfaceBuffer();
        bindings.vertexBufferOffset = modelRuntime->getSurfaceBufferOffset();
        bindings.faceIndexBuffer = modelRuntime->getTriangleIndicesBuffer();
        bindings.faceIndexBufferOffset = modelRuntime->getTriangleIndicesBufferOffset();
        bindings.seedPositionBuffer = resources.seedPositionBuffer;
        bindings.seedPositionBufferOffset = resources.seedPositionBufferOffset;
        bindings.candidateBuffer = modelRuntime->getVoronoiCandidateBuffer();
        bindings.candidateBufferOffset = modelRuntime->getVoronoiCandidateBufferOffset();

        voronoiCandidateCompute->updateDescriptors(bindings);
        voronoiCandidateCompute->dispatch(faceCount, receiverDomain->nodeCount, receiverDomain->nodeOffset);
        ++dispatchedCount;
    }

    if (skippedMissingDomain > 0 ||
        skippedMissingGeometryRuntime > 0 ||
        skippedZeroFaces > 0 ||
        skippedMissingCandidateBuffer > 0) {
        std::cerr << "[VoronoiSystem] Voronoi candidate update skipped some receivers"
                  << " dispatched=" << dispatchedCount
                  << " skippedMissingDomain=" << skippedMissingDomain
                  << " skippedMissingGeometryRuntime=" << skippedMissingGeometryRuntime
                  << " skippedZeroFaces=" << skippedZeroFaces
                  << " skippedMissingCandidateBuffer=" << skippedMissingCandidateBuffer
                  << std::endl;
    }
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
    runtime.cleanup(memoryAllocator);
}
