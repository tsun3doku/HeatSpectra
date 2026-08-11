#pragma once

#include <cuda_runtime.h>

#include <iostream>

namespace cudaUtils {

inline bool cudaCheck(cudaError_t result, const char* operation) {
    if (result == cudaSuccess) return true;
    std::cerr << "[CUDA] " << operation << " failed: "
              << cudaGetErrorString(result) << std::endl;
    return false;
}

} // namespace cudaUtils
