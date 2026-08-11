#include "RVDWorkQueue.cuh"

#include "../../cuda/CudaBuffer.cuh"

#include <cub/cub.cuh>
#include <cuda_runtime.h>

#include <cstdint>
#include <iostream>
#include <limits>

namespace voronoi {
namespace {

template <class T>
using DeviceBuffer = cudaUtils::CudaBuffer<T>;
constexpr auto cudaOk = cudaUtils::cudaCheck;

__global__ void classifyKernel(const RVDStatus* statuses, uint8_t* flags,
                               uint32_t count, uint32_t statusMask) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    flags[index] = (statusMask & rvdStatusBit(statuses[index])) != 0u ? 1u : 0u;
}

__global__ void classifyActiveKernel(const RVDStatus* statuses, const uint32_t* ids,
                                     uint8_t* flags, uint32_t count, uint32_t statusMask) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    flags[index] = (statusMask & rvdStatusBit(statuses[ids[index]])) != 0u ? 1u : 0u;
}

__global__ void countActiveStatusesKernel(const RVDStatus* statuses, const uint32_t* ids,
                                          uint32_t count, uint32_t* statusCounts) {
    __shared__ uint32_t blockCounts[static_cast<uint32_t>(RVDStatus::Count)];
    if (threadIdx.x < static_cast<uint32_t>(RVDStatus::Count))
        blockCounts[threadIdx.x] = 0;
    __syncthreads();

    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) {
        const uint32_t status = static_cast<uint32_t>(statuses[ids[index]]);
        if (status < static_cast<uint32_t>(RVDStatus::Count))
            atomicAdd(blockCounts + status, 1u);
    }
    __syncthreads();
    if (threadIdx.x < static_cast<uint32_t>(RVDStatus::Count) && blockCounts[threadIdx.x] != 0)
        atomicAdd(statusCounts + threadIdx.x, blockCounts[threadIdx.x]);
}

} // namespace

class RVDWorkQueue::Implementation {
public:
    bool initialize(uint32_t cellCount, cudaStream_t queueStream) {
        stream = queueStream;
        if (!flags.allocate(cellCount) || !ids[0].allocate(cellCount) || !ids[1].allocate(cellCount) ||
            !count.allocate(1) ||
            !statusCounts.allocate(static_cast<size_t>(RVDStatus::Count))) return false;
        const cub::CountingInputIterator<uint32_t> sequence(0);
        if (!cudaOk(cub::DeviceSelect::Flagged(nullptr, temporaryBytes,
                                                sequence, flags.get(), ids[0].get(),
                                                count.get(), cellCount, stream),
                     "query RVD compaction storage")) return false;
        return temporary.allocate(temporaryBytes);
    }

    bool compactAll(const RVDStatus* statuses, uint32_t cellCount, uint32_t statusMask,
                    uint32_t& selectedCount) {
        if (cellCount == 0) {
            hostCount = selectedCount = 0;
            return true;
        }
        constexpr uint32_t BlockSize = 256;
        classifyKernel<<<(cellCount + BlockSize - 1) / BlockSize, BlockSize, 0, stream>>>
            (statuses, flags.get(), cellCount, statusMask);
        const cub::CountingInputIterator<uint32_t> sequence(0);
        if (!cudaOk(cudaGetLastError(), "classify RVD queue") ||
            !cudaOk(cub::DeviceSelect::Flagged(temporary.get(), temporaryBytes,
                                                sequence, flags.get(), ids[0].get(),
                                                count.get(), cellCount, stream),
                     "compact RVD queue") ||
            !cudaOk(cudaStreamSynchronize(stream), "synchronize RVD queue") ||
            !count.download(&hostCount, 1)) return false;
        selectedCount = hostCount;
        activeBuffer = 0;
        return true;
    }

    bool countActiveStatuses(
        const RVDStatus* statuses, uint32_t activeCount,
        std::array<uint32_t, static_cast<size_t>(RVDStatus::Count)>& hostStatusCounts) {
        hostStatusCounts.fill(0);
        if (activeCount == 0) return true;
        if (!cudaOk(cudaMemsetAsync(statusCounts.get(), 0,
                                    hostStatusCounts.size() * sizeof(uint32_t), stream),
                    "clear RVD status counters")) return false;
        constexpr uint32_t BlockSize = 256;
        countActiveStatusesKernel<<<(activeCount + BlockSize - 1) / BlockSize,
                                    BlockSize, 0, stream>>>(
            statuses, ids[activeBuffer].get(), activeCount, statusCounts.get());
        return cudaOk(cudaGetLastError(), "count active RVD statuses") &&
               cudaOk(cudaStreamSynchronize(stream), "synchronize RVD status counters") &&
               statusCounts.download(hostStatusCounts.data(), hostStatusCounts.size());
    }

    bool compactActive(const RVDStatus* statuses, uint32_t activeCount,
                       uint32_t statusMask, uint32_t& selectedCount) {
        if (activeCount == 0) {
            hostCount = selectedCount = 0;
            return true;
        }
        constexpr uint32_t BlockSize = 256;
        classifyActiveKernel<<<(activeCount + BlockSize - 1) / BlockSize,
                               BlockSize, 0, stream>>>(
            statuses, ids[activeBuffer].get(), flags.get(), activeCount, statusMask);
        const uint32_t nextBuffer = 1u - activeBuffer;
        if (!cudaOk(cudaGetLastError(), "classify active RVD queue") ||
            !cudaOk(cub::DeviceSelect::Flagged(temporary.get(), temporaryBytes,
                                                ids[activeBuffer].get(), flags.get(),
                                                ids[nextBuffer].get(), count.get(),
                                                activeCount, stream),
                    "compact active RVD queue") ||
            !cudaOk(cudaStreamSynchronize(stream), "synchronize active RVD queue") ||
            !count.download(&hostCount, 1)) return false;
        selectedCount = hostCount;
        activeBuffer = nextBuffer;
        return true;
    }

    cudaStream_t stream = nullptr;
    DeviceBuffer<uint8_t> flags;
    DeviceBuffer<uint32_t> ids[2];
    DeviceBuffer<uint32_t> count;
    DeviceBuffer<uint32_t> statusCounts;
    DeviceBuffer<uint8_t> temporary;
    size_t temporaryBytes = 0;
    uint32_t hostCount = 0;
    uint32_t activeBuffer = 0;
};

RVDWorkQueue::RVDWorkQueue() : implementation(std::make_unique<Implementation>()) {}
RVDWorkQueue::~RVDWorkQueue() = default;

bool RVDWorkQueue::initialize(uint32_t cellCount, cudaStream_t stream) {
    return implementation->initialize(cellCount, stream);
}

bool RVDWorkQueue::compactAll(const RVDStatus* statuses, uint32_t cellCount,
                              uint32_t statusMask, uint32_t& selectedCount) {
    return implementation->compactAll(statuses, cellCount, statusMask, selectedCount);
}

bool RVDWorkQueue::countActiveStatuses(
    const RVDStatus* statuses, uint32_t activeCount,
    std::array<uint32_t, static_cast<size_t>(RVDStatus::Count)>& statusCounts) {
    return implementation->countActiveStatuses(statuses, activeCount, statusCounts);
}

bool RVDWorkQueue::compactActive(const RVDStatus* statuses, uint32_t activeCount,
                                 uint32_t statusMask, uint32_t& selectedCount) {
    return implementation->compactActive(statuses, activeCount, statusMask, selectedCount);
}

const uint32_t* RVDWorkQueue::deviceQueue() const {
    return implementation->ids[implementation->activeBuffer].get();
}

bool RVDWorkQueue::download(std::vector<uint32_t>& cellIds) const {
    cellIds.resize(implementation->hostCount);
    return implementation->ids[implementation->activeBuffer].download(cellIds.data(), cellIds.size());
}

} // namespace voronoi
