#pragma once

#include "RVDStatus.cuh"

#include <cuda_runtime_api.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace voronoi {

class RVDWorkQueue {
public:
    RVDWorkQueue();
    ~RVDWorkQueue();

    RVDWorkQueue(const RVDWorkQueue&) = delete;
    RVDWorkQueue& operator=(const RVDWorkQueue&) = delete;

    bool initialize(uint32_t cellCount, cudaStream_t stream);
    bool compactAll(const RVDStatus* statuses, uint32_t cellCount, uint32_t statusMask, uint32_t& selectedCount);
    bool countActiveStatuses(const RVDStatus* statuses, uint32_t activeCount, std::array<uint32_t, static_cast<size_t>(RVDStatus::Count)>& statusCounts);
    bool compactActive(const RVDStatus* statuses, uint32_t activeCount, uint32_t statusMask, uint32_t& selectedCount);
    const uint32_t* deviceQueue() const;
    bool download(std::vector<uint32_t>& cellIds) const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

} // namespace voronoi
