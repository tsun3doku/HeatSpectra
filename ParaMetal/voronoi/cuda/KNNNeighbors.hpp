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

class KNNNeighbors {
public:
    KNNNeighbors();
    ~KNNNeighbors();

    KNNNeighbors(const KNNNeighbors&) = delete;
    KNNNeighbors& operator=(const KNNNeighbors&) = delete;

    bool initialize(VulkanDevice& vulkanDevice);

    bool build(const std::vector<glm::vec4>& seeds, uint32_t k, const std::vector<uint32_t>& queryIds);
    bool query(const uint32_t* deviceQueryIds, uint32_t count, uint32_t k, KNNBatch& batch);

    void cleanup();

    const float* deviceSeeds() const;        
    const uint32_t* deviceNeighborIds() const; 
    uint32_t neighborStride() const;     
    uint32_t seedCount() const;

    bool downloadPrefix(uint32_t maxNeighbors, std::vector<uint32_t>& out) const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

} // namespace voronoi
