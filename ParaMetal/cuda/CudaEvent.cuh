#pragma once

#include "CudaCheck.cuh"

#include <cuda_runtime.h>

#include <utility>

namespace cudaUtils {

class CudaEvent {
public:
    CudaEvent() = default;
    ~CudaEvent() { reset(); }

    CudaEvent(const CudaEvent&) = delete;
    CudaEvent& operator=(const CudaEvent&) = delete;

    CudaEvent(CudaEvent&& other) noexcept
        : event(std::exchange(other.event, nullptr)) {}

    CudaEvent& operator=(CudaEvent&& other) noexcept {
        if (this != &other) {
            reset();
            event = std::exchange(other.event, nullptr);
        }
        return *this;
    }

    bool create(unsigned flags = cudaEventDefault) {
        if (!reset()) return false;
        return cudaCheck(cudaEventCreateWithFlags(&event, flags), "create CUDA event");
    }

    bool record(cudaStream_t stream) {
        return cudaCheck(cudaEventRecord(event, stream), "record CUDA event");
    }

    bool synchronize() {
        return cudaCheck(cudaEventSynchronize(event), "synchronize CUDA event");
    }

    bool elapsedSince(const CudaEvent& begin, float& milliseconds) const {
        return cudaCheck(cudaEventElapsedTime(&milliseconds, begin.event, event),
                         "measure CUDA event interval");
    }

    bool reset() {
        if (!event) return true;
        cudaEvent_t oldEvent = std::exchange(event, nullptr);
        return cudaCheck(cudaEventDestroy(oldEvent), "destroy CUDA event");
    }

private:
    cudaEvent_t event = nullptr;
};

} // namespace cudaUtils
