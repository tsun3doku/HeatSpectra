#include "VoronoiSystem.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiCandidateCompute.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <glm/mat4x4.hpp>
#include <iostream>

VoronoiSystem::VoronoiSystem(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      commandPool(commandPool) {
    voronoiSystemBuildStage = std::make_unique<VoronoiSystemBuildStage>(vulkanDevice, memoryAllocator, commandPool);
    voronoiCandidateCompute = std::make_unique<VoronoiCandidateCompute>(vulkanDevice, commandPool);
    voronoiCandidateCompute->initialize();
}

VoronoiSystem::~VoronoiSystem() = default;

void VoronoiSystem::setMeshGeometry(
    const std::vector<glm::vec3>& geometryPositions,
    const std::vector<uint32_t>& geometryTriangleIndices,
    const std::vector<voronoi::SurfaceVertex>& surfaceVertices,
    const std::vector<uint32_t>& surfaceTriangleIndices,
    uint32_t runtimeModelId,
    const glm::mat4& meshModelMatrix) {
    runtime.setMeshGeometry(
        vulkanDevice,
        memoryAllocator,
        commandPool,
        geometryPositions,
        geometryTriangleIndices,
        surfaceVertices,
        surfaceTriangleIndices,
        runtimeModelId,
        meshModelMatrix);

}

void VoronoiSystem::setPointGeometry(
    const std::vector<glm::vec4>& positions,
    const std::array<glm::vec3, 8>& domainCorners) {
    runtime.setPointGeometry(
        vulkanDevice,
        memoryAllocator,
        commandPool,
        0,  
        positions,
        domainCorners);
}

void VoronoiSystem::setSeedPositions(
    const std::vector<glm::vec4>& positions,
    const std::array<glm::vec3, 8>& domainCorners) {
    runtime.setSeedPositions(positions, domainCorners);
}

void VoronoiSystem::clearGeometry() {
    runtime.clearGeometry();
}

void VoronoiSystem::setParams(float cellSize, int voxelResolution) {
    runtime.setParams(cellSize, voxelResolution);
}

bool VoronoiSystem::ensureConfigured() {
    if (!runtime.isReady()) {
        if (!rebuildVoronoiRuntime()) {
            std::cerr << "[VoronoiSystem] rebuildVoronoiRuntime returned false" << std::endl;
            return false;
        }
    }

    dispatchVoronoiCandidateUpdates();
    return true;
}

bool VoronoiSystem::rebuildVoronoiRuntime() {
    if (!voronoiSystemBuildStage->prepareDomainGeometry(
            runtime, runtime.getCellSize(), runtime.getVoxelResolution())) {
        std::cerr << "[VoronoiSystem] prepareDomainGeometry returned false" << std::endl;
        return false;
    }

    runtime.reorderSeeds();
    if (!voronoiSystemBuildStage->buildNodeDomain(runtime, K_NEIGHBORS)) {
        std::cerr << "[VoronoiSystem] buildNodeDomain returned false" << std::endl;
        return false;
    }

    if (voronoiSystemBuildStage->getCandidateNodeCount() == 0) {
        return false;
    }

    VoronoiDomainRuntime* domainRuntime = runtime.getDomainRuntime();
    if (!domainRuntime) return false;
    if (!domainRuntime->isPointDomain()) {
        auto* modelRuntime = static_cast<VoronoiModelRuntime*>(domainRuntime);
        if (!modelRuntime->buildAndStageSurfaceMappings(runtime.getNodeDomain(), runtime.getVoxelGrid())) {
            return false;
        }
    }

    runtime.markReady();
    return true;
}

void VoronoiSystem::dispatchVoronoiCandidateUpdates() {
    if (!voronoiCandidateCompute || voronoiSystemBuildStage->getCandidateNodeCount() == 0) {
        return;
    }

    VoronoiDomainRuntime* domainRuntime = runtime.getDomainRuntime();
    if (!domainRuntime) {
        return;
    }

    // Point domains have no triangle faces 
    if (domainRuntime->isPointDomain()) {
        return;
    }

    VoronoiModelRuntime* modelRuntime = static_cast<VoronoiModelRuntime*>(domainRuntime);
    uint32_t faceCount = static_cast<uint32_t>(modelRuntime->getSurfaceTriangleCount());
    if (modelRuntime->getSurfaceBuffer() == VK_NULL_HANDLE) {
        return;
    }
    if (faceCount == 0) {
        return;
    }
    if (modelRuntime->getCandidateBuffer() == VK_NULL_HANDLE) {
        return;
    }

    VoronoiCandidateCompute::Bindings bindings{};
    bindings.vertexBuffer = modelRuntime->getSurfaceBuffer();
    bindings.vertexBufferOffset = modelRuntime->getSurfaceBufferOffset();
    bindings.faceIndexBuffer = modelRuntime->getTriangleIndicesBuffer();
    bindings.faceIndexBufferOffset = modelRuntime->getTriangleIndicesBufferOffset();
    bindings.seedPositionBuffer = voronoiSystemBuildStage->getSeedPositionBuffer();
    bindings.seedPositionBufferOffset = voronoiSystemBuildStage->getSeedPositionBufferOffset();
    bindings.candidateBuffer = modelRuntime->getCandidateBuffer();
    bindings.candidateBufferOffset = modelRuntime->getCandidateBufferOffset();

    voronoiCandidateCompute->updateDescriptors(bindings);
    voronoiCandidateCompute->dispatch(faceCount, voronoiSystemBuildStage->getCandidateNodeCount());

}

void VoronoiSystem::cleanupResources() {
    if (voronoiSystemBuildStage) {
        voronoiSystemBuildStage->cleanupResources();
    }
    if (voronoiCandidateCompute) {
        voronoiCandidateCompute->cleanupResources();
    }
}

void VoronoiSystem::cleanup() {
    if (voronoiSystemBuildStage) {
        voronoiSystemBuildStage->cleanup();
    }
    runtime.cleanup();
}
