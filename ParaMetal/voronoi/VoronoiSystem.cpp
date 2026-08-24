#include "VoronoiSystem.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiCandidateCompute.hpp"

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

void VoronoiSystem::setGlobalGeometry(
    const std::vector<uint32_t>& runtimeModelIds,
    const std::vector<std::vector<glm::vec3>>& positions,
    const std::vector<std::vector<uint32_t>>& triangleIndices,
    const std::vector<std::vector<glm::vec3>>& surfacePositions,
    const std::vector<std::vector<uint32_t>>& surfaceTriangleIndices,
    float sdfPadding) {
    runtime.setGlobalGeometry(runtimeModelIds, positions, triangleIndices, surfacePositions, surfaceTriangleIndices, sdfPadding);
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
    if (runtime.isGlobalDomain()) {
        if (!voronoiSystemBuildStage->buildGlobalDomain(runtime)) {
            std::cerr << "[VoronoiSystem] buildGlobalDomain returned false" << std::endl;
            return false;
        }
        voronoiSystemBuildStage->buildGlobalDisplayCandidates(runtime);
        runtime.markReady();
        return true;
    }

    runtime.getSeedFlags().assign(runtime.getSeedPositions().size(), 0u);
    runtime.setVoxelGridBuilt(false);
    runtime.reorderSeeds();
    if (!voronoiSystemBuildStage->buildNodeDomain(runtime, K_NEIGHBORS)) {
        std::cerr << "[VoronoiSystem] buildNodeDomain returned false" << std::endl;
        return false;
    }

    if (voronoiSystemBuildStage->getCandidateNodeCount() == 0) {
        return false;
    }

    runtime.markReady();
    return true;
}

void VoronoiSystem::dispatchVoronoiCandidateUpdates() {
    if (!voronoiCandidateCompute || voronoiSystemBuildStage->getCandidateNodeCount() == 0) {
        return;
    }

    if (!runtime.isGlobalDomain()) {
        return;
    }

    const auto* candidate = voronoiSystemBuildStage->getCandidate();
    if (!candidate) {
        return;
    }

    const VkBuffer seedPositionBuffer = voronoiSystemBuildStage->getSeedPositionBuffer();
    const VkDeviceSize seedPositionBufferOffset = voronoiSystemBuildStage->getSeedPositionBufferOffset();
    const uint32_t seedCount = voronoiSystemBuildStage->getCandidateNodeCount();

    for (size_t i = 0; i < candidate->getModelCount(); ++i) {
        if (candidate->getFaceCount(i) == 0 || candidate->getCandidateBuffer(i) == VK_NULL_HANDLE) {
            continue;
        }

        voronoiCandidateCompute->updateDescriptors(
            candidate->getVertexBuffer(i),
            candidate->getVertexBufferOffset(i),
            candidate->getFaceIndexBuffer(i),
            candidate->getFaceIndexBufferOffset(i),
            seedPositionBuffer,
            seedPositionBufferOffset,
            candidate->getCandidateBuffer(i),
            candidate->getCandidateBufferOffset(i));

        voronoiCandidateCompute->dispatch(candidate->getFaceCount(i), seedCount);
    }
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
