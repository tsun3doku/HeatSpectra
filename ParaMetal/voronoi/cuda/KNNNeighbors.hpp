#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

class VulkanDevice;

namespace voronoi {

class KNNBatch {
public:
    KNNBatch();
    ~KNNBatch();
    KNNBatch(KNNBatch&&) noexcept;
    KNNBatch& operator=(KNNBatch&&) noexcept;
    KNNBatch(const KNNBatch&) = delete;
    KNNBatch& operator=(const KNNBatch&) = delete;

    const uint32_t* deviceNeighborIds() const;
    uint32_t neighborStride() const;

private:
    class Implementation;
    friend class KNNNeighbors;
    std::unique_ptr<Implementation> implementation;
};

// CUDA-resident exact K-nearest-neighbor index over Voronoi seed positions.
//
// Owned by VoronoiSystemBuildStage and initialized on the same Vulkan-matched
// CUDA device as RVD. The authored node-graph seed points remain authoritative;
// KNN only produces neighbor IDs and never generates, removes, or moves seeds.
//
// Seed positions, the spatial index, and the requested base neighbor rows stay
// resident on the GPU. Adaptive queries return compact KNNBatch objects.
// Only the prefix required for Vulkan display or CPU point-coupling is
// downloaded via downloadPrefix().
class KNNNeighbors {
public:
    KNNNeighbors();
    ~KNNNeighbors();

    KNNNeighbors(const KNNNeighbors&) = delete;
    KNNNeighbors& operator=(const KNNNeighbors&) = delete;

    // Selects the CUDA device whose UUID matches the Vulkan physical device
    // (same matching as RVD) and creates a non-blocking stream. Used in
    // production by VoronoiSystemBuildStage.
    bool initialize(VulkanDevice& vulkanDevice);

    void cleanup();

    // Build the uniform grid over the (already Morton-reordered) seeds and
    // fill exactly k entries for the requested query rows. Unrequested rows
    // remain UINT32_MAX. k must be greater than zero and less than the seed
    // count. k is RVDNormalCandidateCount (50) for mesh domains and
    // maxNeighbors for point domains. The search is exact: it uses
    // double-precision squared distances, excludes the query seed, and sorts
    // by (distance, seed ID).
    bool build(const std::vector<glm::vec4>& seeds, uint32_t k,
               const std::vector<uint32_t>& queryIds);

    // Query selected seed IDs into a compact count x k batch. The
    // persistent spatial index is reused; no global maximum-width row matrix
    // is allocated or mutated.
    bool query(const uint32_t* deviceQueryIds, uint32_t count, uint32_t k,
               KNNBatch& batch);

    // Read-only device views consumed directly by RVD kernels (no re-upload).
    const float*    deviceSeeds() const;        // float4[seedCount()], device memory
    const uint32_t* deviceNeighborIds() const;  // uint32_t[seedCount()*neighborStride()]
    uint32_t        neighborStride() const;     // base k selected by build()
    uint32_t        seedCount() const;

    // Download the first maxNeighbors entries of every row to a flat host
    // vector (row-major, stride == maxNeighbors) for Vulkan display-buffer
    // upload and CPU point-coupling construction.
    bool downloadPrefix(uint32_t maxNeighbors, std::vector<uint32_t>& out) const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

} // namespace voronoi
