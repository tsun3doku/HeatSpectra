#pragma once

#include "KNNNeighbors.hpp"

#include "../../cuda/CudaBuffer.cuh"

namespace voronoi {

class KNNBatch::Implementation {
public:
  cudaUtils::CudaBuffer<uint32_t> neighbors;
  uint32_t neighborStride = 0;
};

} // namespace voronoi
