#pragma once

#include "voronoi/VoronoiRuntime.hpp"
#include "voronoi/VoronoiSystemBuildStage.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class VoronoiModelRuntime;
class MemoryAllocator;
class VulkanDevice;
class CommandPool;
class VoronoiCandidateCompute;

class VoronoiSystem {
public:
    VoronoiSystem(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& commandPool);
    ~VoronoiSystem();

    bool isReady() const { return runtime.isReady(); }

    void setMeshGeometry(
        const std::vector<glm::vec3>& geometryPositions,
        const std::vector<uint32_t>& geometryTriangleIndices,
        const std::vector<voronoi::SurfaceVertex>& surfaceVertices,
        const std::vector<uint32_t>& surfaceTriangleIndices,
        uint32_t runtimeModelId,
        const glm::mat4& meshModelMatrix);
    void setPointGeometry(
        const std::vector<glm::vec4>& positions,
        const std::array<glm::vec3, 8>& domainCorners);
    void setSeedPositions(
        const std::vector<glm::vec4>& positions,
        const std::array<glm::vec3, 8>& domainCorners);
    void clearGeometry();
    void setParams(float cellSize, int voxelResolution);
    bool ensureConfigured();

    VoronoiDomainRuntime* getDomainRuntime() const { return runtime.getDomainRuntime(); }
    uint32_t getCandidateNodeCount() const { return voronoiSystemBuildStage->getCandidateNodeCount(); }
    const VoronoiSystemBuildStage& getBuildStage() const { return *voronoiSystemBuildStage; }
    VoronoiRuntime& runtimeRef() { return runtime; }
    const VoronoiRuntime& runtimeRef() const { return runtime; }

    void cleanupResources();
    void cleanup();

private:
    bool rebuildVoronoiRuntime();
    void dispatchVoronoiCandidateUpdates();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;
    VoronoiRuntime runtime;

    std::unique_ptr<VoronoiSystemBuildStage> voronoiSystemBuildStage;
    std::unique_ptr<VoronoiCandidateCompute> voronoiCandidateCompute;

    static constexpr int K_NEIGHBORS = 50;
};
