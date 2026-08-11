#pragma once

#include "CudaCheck.cuh"

#include <cstddef>
#include <limits>
#include <utility>

namespace cudaUtils {

template <class T>
class CudaBuffer {
public:
    CudaBuffer() = default;
    ~CudaBuffer() { reset(); }

    CudaBuffer(const CudaBuffer&) = delete;
    CudaBuffer& operator=(const CudaBuffer&) = delete;

    CudaBuffer(CudaBuffer&& other) noexcept
        : data(std::exchange(other.data, nullptr)),
          elementCount(std::exchange(other.elementCount, 0)) {}

    CudaBuffer& operator=(CudaBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            data = std::exchange(other.data, nullptr);
            elementCount = std::exchange(other.elementCount, 0);
        }
        return *this;
    }

    bool allocate(size_t count) {
        reset();
        if (count == 0) return true;
        if (count > std::numeric_limits<size_t>::max() / sizeof(T)) return false;
        if (!cudaCheck(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T)),
                       "allocate device buffer")) return false;
        elementCount = count;
        return true;
    }

    bool upload(const T* source, size_t count) {
        return count <= elementCount &&
               (count == 0 || cudaCheck(cudaMemcpy(data, source, count * sizeof(T),
                                                   cudaMemcpyHostToDevice),
                                        "upload device buffer"));
    }

    bool download(T* destination, size_t count) const {
        return count <= elementCount &&
               (count == 0 || cudaCheck(cudaMemcpy(destination, data, count * sizeof(T),
                                                   cudaMemcpyDeviceToHost),
                                        "download device buffer"));
    }

    bool uploadAsync(const T* source, size_t count, cudaStream_t stream) {
        return count <= elementCount &&
               (count == 0 || cudaCheck(cudaMemcpyAsync(data, source, count * sizeof(T),
                                                        cudaMemcpyHostToDevice, stream),
                                        "upload device buffer asynchronously"));
    }

    bool downloadAsync(T* destination, size_t count, cudaStream_t stream) const {
        return count <= elementCount &&
               (count == 0 || cudaCheck(cudaMemcpyAsync(destination, data, count * sizeof(T),
                                                        cudaMemcpyDeviceToHost, stream),
                                        "download device buffer asynchronously"));
    }

    void reset() {
        if (data) cudaFree(data);
        data = nullptr;
        elementCount = 0;
    }

    T* get() { return data; }
    const T* get() const { return data; }
    size_t size() const { return elementCount; }

private:
    T* data = nullptr;
    size_t elementCount = 0;
};

} // namespace cudaUtils
