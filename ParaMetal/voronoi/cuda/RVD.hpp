#pragma once

#include "../VoronoiGpuStructs.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

class VoxelGrid;
class VulkanDevice;

namespace voronoi {

class KNNNeighbors;

constexpr uint32_t RVDNormalCandidateCount = 50;
constexpr uint32_t RVDRetryCandidateCount = 192;
constexpr uint32_t RVDMaximumCandidateCount = UINT16_MAX;
constexpr uint32_t RVDRetryPlaneCount = 256;
constexpr uint32_t RVDRetryVertexCount = 256;
constexpr uint32_t RVDRetryTriangleCount = 768;
constexpr uint32_t RVDMaximumInterfaces = RVDRetryPlaneCount;

enum class RVDPredicateMode : uint32_t {
    Adaptive,
    Float,
    Double
};

struct RVDConfig {
    struct Capacity {
        uint32_t K = RVDRetryCandidateCount;
        uint32_t P = RVDRetryPlaneCount;
        uint32_t V = RVDRetryVertexCount;
        uint32_t T = RVDRetryTriangleCount;
    };

    struct AdaptivePolicy {
        Capacity initial{};
        Capacity maximum{RVDMaximumCandidateCount, RVDRetryPlaneCount,
                         RVDRetryVertexCount, RVDRetryTriangleCount};
        float growthFactor = 1.5f;
        uint32_t maximumPasses = 5;
    } adaptive;

    RVDPredicateMode predicateMode = RVDPredicateMode::Adaptive;
    uint32_t baseChunkSize = 8192;
    uint32_t restrictedChunkSize = 1024;
};

struct RVDInput {
    const std::vector<glm::vec4>* seeds = nullptr;
    const std::vector<uint32_t>* seedFlags = nullptr;
    KNNNeighbors* candidateNeighbors = nullptr;
    const std::array<glm::vec3, 8>* domainCorners = nullptr;
    float nominalCellSize = 0.0f;
};

struct RVDResult {
    std::vector<Node> nodes;
    std::vector<uint32_t> nodeFlags;
    std::vector<float> surfacePatchAreas;
    std::vector<float> interfaceAreas;
    std::vector<uint32_t> interfaceNeighborIds;
};

class RVD {
public:
    RVD();
    ~RVD();

    RVD(const RVD&) = delete;
    RVD& operator=(const RVD&) = delete;

    bool initialize(VulkanDevice& vulkanDevice);
    bool prepareMeshGeometry(
        const std::vector<std::array<glm::vec4, 3>>& meshTriangles,
        const VoxelGrid& voxelGrid,
        const std::vector<glm::vec4>& seeds,
        const glm::vec3& sdfGridMin,
        const glm::ivec3& sdfGridDim,
        float nominalCellSize,
        std::vector<uint32_t>& seedFlags);
    bool hasPreparedMeshGeometry() const;
    void clearMeshGeometry();
    bool build(const RVDInput& input, const RVDConfig& config, RVDResult& result);
    void cleanup();

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

} // namespace voronoi
