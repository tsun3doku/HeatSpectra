#pragma once

#include "KNNBatch.cuh"
#include "KNNNeighborsKernels.cuh"

#include "../../cuda/CudaBuffer.cuh"

#include <cuda_runtime.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace voronoi {

class KNNNeighbors::Implementation {
public:
  Implementation() = default;
  ~Implementation();

  bool initialize(VulkanDevice &vulkanDevice);
  void cleanup();
  bool build(const std::vector<glm::vec4> &seeds, uint32_t neighborCount,
             const std::vector<uint32_t> &queryIds);
  std::unique_ptr<KNNBatch::Implementation>
  query(const uint32_t *deviceQueryIds, uint32_t queryCount,
        uint32_t neighborCount);
  bool downloadPrefix(uint32_t neighborCount,
                      std::vector<uint32_t> &output) const;

  const float *deviceSeeds() const;
  const uint32_t *deviceNeighborIds() const;
  uint32_t neighborStride() const;
  uint32_t seedCount() const;

private:
  bool setupDevice(int ordinal);
  bool buildCellOffsets();
  bool buildGrid(const std::vector<glm::vec4> &seeds);
  DeviceKnn makeDeviceKnn() const;
  bool buildNeighbors(uint32_t neighborCount,
                      const std::vector<uint32_t> &queryIds);

  int cudaDeviceOrdinal = -1;
  cudaStream_t stream = nullptr;
  bool initialized = false;
  cudaUtils::CudaBuffer<float4> seeds;
  cudaUtils::CudaBuffer<uint32_t> neighbors;
  cudaUtils::CudaBuffer<double> distanceHeap;
  cudaUtils::CudaBuffer<uint32_t> searchFailure;
  cudaUtils::CudaBuffer<BucketEntry> bucketEntries;
  cudaUtils::CudaBuffer<uint32_t> cellStart;
  cudaUtils::CudaBuffer<int3> cellOffsets;
  cudaUtils::CudaBuffer<double> cellOffsetDistances;
  mutable cudaUtils::CudaBuffer<uint32_t> prefixBuffer;
  DeviceGrid grid{};
  uint32_t seedCountValue = 0;
  uint32_t neighborStrideValue = 0;
  uint32_t cellOffsetCount = 0;
};

} // namespace voronoi
