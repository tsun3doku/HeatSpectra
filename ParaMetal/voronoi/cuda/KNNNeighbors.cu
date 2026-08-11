#include "KNNNeighbors.hpp"

#include "KNNNeighbors.cuh"

#include "../../cuda/CudaEvent.cuh"
#include "../../cuda/CudaVulkanDevice.hpp"
#include "../../vulkan/VulkanDevice.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace voronoi {

constexpr double TargetOccupancy = 3.1;

template <class T> using DeviceBuffer = cudaUtils::CudaBuffer<T>;
constexpr auto cudaOk = cudaUtils::cudaCheck;

struct HostAabb {
  double minx, miny, minz, maxx, maxy, maxz;
};

static bool computeAabb(const std::vector<glm::vec4> &seeds, HostAabb &aabb) {
  if (seeds.empty())
    return false;
  aabb.minx = aabb.miny = aabb.minz = std::numeric_limits<double>::max();
  aabb.maxx = aabb.maxy = aabb.maxz = std::numeric_limits<double>::lowest();
  for (const glm::vec4 &seed : seeds) {
    if (!std::isfinite(seed.x) || !std::isfinite(seed.y) ||
        !std::isfinite(seed.z))
      return false;
    aabb.minx = std::min(aabb.minx, double(seed.x));
    aabb.miny = std::min(aabb.miny, double(seed.y));
    aabb.minz = std::min(aabb.minz, double(seed.z));
    aabb.maxx = std::max(aabb.maxx, double(seed.x));
    aabb.maxy = std::max(aabb.maxy, double(seed.y));
    aabb.maxz = std::max(aabb.maxz, double(seed.z));
  }
  return true;
}

static bool computeGrid(const HostAabb &aabb, uint32_t seedCount,
                        DeviceGrid &grid) {
  const double extentX = aabb.maxx - aabb.minx;
  const double extentY = aabb.maxy - aabb.miny;
  const double extentZ = aabb.maxz - aabb.minz;
  const double maximumExtent = std::max({extentX, extentY, extentZ});
  if (!(maximumExtent > 0.0) || !std::isfinite(maximumExtent))
    return false;

  const double dimensionValue = std::cbrt(double(seedCount) / TargetOccupancy);
  if (!std::isfinite(dimensionValue) ||
      dimensionValue > double(std::numeric_limits<int>::max())) {
    return false;
  }
  const int dimension = std::max(1, int(std::llround(dimensionValue)));
  const double cellWidth = maximumExtent / double(dimension);
  if (!(cellWidth > 0.0) || !std::isfinite(cellWidth))
    return false;

  grid.gridDim = make_int3(dimension, dimension, dimension);
  grid.dimXY = grid.gridDim.x * grid.gridDim.y;
  grid.totalCells = uint32_t(grid.gridDim.x) * uint32_t(grid.gridDim.y) *
                    uint32_t(grid.gridDim.z);
  if (grid.totalCells == 0)
    return false;
  grid.invCellWidth = float(1.0 / cellWidth);
  grid.gridMin =
      make_float3(float(aabb.minx), float(aabb.miny), float(aabb.minz));
  return grid.invCellWidth > 0.0f && std::isfinite(grid.invCellWidth);
}

static bool launchKnnChunks(const DeviceKnn &knn, uint32_t *neighbors,
                            double *distanceHeap, uint32_t *searchFailure,
                            const uint32_t *queryIds, uint32_t queryCount,
                            uint32_t neighborCount, uint32_t chunkSize,
                            uint32_t outputStride, bool scatterBySeedId,
                            cudaStream_t stream) {
  if (queryCount == 0)
    return true;

  cudaUtils::CudaEvent chunkBegin;
  cudaUtils::CudaEvent chunkEnd;
  if (!chunkBegin.create() || !chunkEnd.create())
    return false;

  uint32_t currentChunkSize = std::max(1u, chunkSize);
  for (uint32_t begin = 0; begin < queryCount;) {
    const uint32_t chunkCount = std::min(currentChunkSize, queryCount - begin);
    if (!chunkBegin.record(stream))
      return false;

    const uint32_t blocks = (chunkCount + KNNBlockSize - 1) / KNNBlockSize;
    uint32_t outputBase = begin;
    if (scatterBySeedId)
      outputBase = 0;
    knnKernel<<<blocks, KNNBlockSize, 0, stream>>>(
        knn, neighbors, distanceHeap, neighborCount, searchFailure,
        queryIds + begin, chunkCount, outputBase, outputStride,
        scatterBySeedId);

    if (!cudaOk(cudaGetLastError(), "launch KNN kernel") ||
        !chunkEnd.record(stream) || !chunkEnd.synchronize())
      return false;

    float chunkMilliseconds = 0.0f;
    if (!chunkEnd.elapsedSince(chunkBegin, chunkMilliseconds))
      return false;
    if (chunkMilliseconds > 100.0f && currentChunkSize > 1) {
      currentChunkSize = std::max(1u, currentChunkSize / 2);
    }
    begin += chunkCount;
  }

  const bool endReset = chunkEnd.reset();
  const bool beginReset = chunkBegin.reset();
  return endReset && beginReset;
}

KNNNeighbors::Implementation::~Implementation() { cleanup(); }

bool KNNNeighbors::Implementation::initialize(VulkanDevice &vulkanDevice) {
  const int ordinal = cudaVulkan::findDevice(vulkanDevice.getPhysicalDevice());
  if (ordinal < 0) {
    std::cerr << "[KNN] Vulkan physical device has no matching CUDA UUID"
              << std::endl;
    return false;
  }
  return setupDevice(ordinal);
}

bool KNNNeighbors::Implementation::setupDevice(int ordinal) {
  cleanup();
  if (ordinal < 0)
    return false;

  int deviceCount = 0;
  if (cudaGetDeviceCount(&deviceCount) != cudaSuccess ||
      ordinal >= deviceCount) {
    std::cerr << "[KNN] invalid CUDA device ordinal " << ordinal << std::endl;
    return false;
  }

  cudaDeviceOrdinal = ordinal;
  if (!cudaOk(cudaSetDevice(cudaDeviceOrdinal), "select KNN CUDA device") ||
      !cudaOk(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking),
              "create KNN stream")) {
    cleanup();
    return false;
  }
  initialized = true;
  return true;
}

void KNNNeighbors::Implementation::cleanup() {
  if (cudaDeviceOrdinal >= 0)
    cudaSetDevice(cudaDeviceOrdinal);
  seeds.reset();
  neighbors.reset();
  distanceHeap.reset();
  bucketEntries.reset();
  cellStart.reset();
  cellOffsets.reset();
  cellOffsetDistances.reset();
  prefixBuffer.reset();
  searchFailure.reset();
  if (stream)
    cudaStreamDestroy(stream);
  stream = nullptr;
  cudaDeviceOrdinal = -1;
  initialized = false;
  seedCountValue = 0;
  neighborStrideValue = 0;
  cellOffsetCount = 0;
}

bool KNNNeighbors::Implementation::build(
    const std::vector<glm::vec4> &seedsIn, uint32_t k,
    const std::vector<uint32_t> &queryIds) {
  if (!initialized || seedsIn.empty() || queryIds.empty() || k == 0 ||
      k >= seedsIn.size())
    return false;
  for (uint32_t queryId : queryIds) {
    if (queryId >= seedsIn.size())
      return false;
  }

  seedCountValue = uint32_t(seedsIn.size());
  neighborStrideValue = k;
  HostAabb aabb{};
  if (!computeAabb(seedsIn, aabb)) {
    std::cerr << "[KNN] non-finite seed in input" << std::endl;
    return false;
  }
  if (!computeGrid(aabb, seedCountValue, grid)) {
    std::cerr << "[KNN] failed to construct a finite search grid" << std::endl;
    return false;
  }

  if (!seeds.allocate(seedCountValue) ||
      !neighbors.allocate(size_t(seedCountValue) * neighborStrideValue) ||
      !distanceHeap.allocate(
          size_t(std::min<uint32_t>(uint32_t(queryIds.size()), 8192u)) * k) ||
      !searchFailure.allocate(1) || !bucketEntries.allocate(seedCountValue) ||
      !cellStart.allocate(grid.totalCells + 1))
    return false;

  static_assert(sizeof(glm::vec4) == sizeof(float4));
  if (!seeds.uploadAsync(reinterpret_cast<const float4 *>(seedsIn.data()),
                         seedCountValue, stream))
    return false;
  if (!cudaOk(cudaMemsetAsync(neighbors.get(), 0xff,
                              size_t(seedCountValue) * neighborStrideValue *
                                  sizeof(uint32_t),
                              stream),
              "clear KNN rows"))
    return false;

  return buildGrid(seedsIn) && buildCellOffsets() &&
         buildNeighbors(k, queryIds);
}

bool KNNNeighbors::Implementation::buildCellOffsets() {
  const int maximumRing =
      std::max({grid.gridDim.x, grid.gridDim.y, grid.gridDim.z});
  if (maximumRing <= 0)
    return false;

  std::vector<int3> offsets;
  std::vector<double> distances;
  offsets.push_back(make_int3(0, 0, 0));
  distances.push_back(0.0);
  for (int ring = 1; ring < maximumRing; ++ring) {
    const double conservativeCellWidth =
        std::nextafter(1.0 / double(grid.invCellWidth), 0.0);
    const double separation = std::max(
        0.0, std::nextafter((double(ring) - 1.0) * conservativeCellWidth, 0.0));
    const double lowerBound =
        separation > 0.0 ? std::nextafter(separation * separation, 0.0) : 0.0;
    for (int z = -ring; z <= ring; ++z) {
      for (int y = -ring; y <= ring; ++y) {
        for (int x = -ring; x <= ring; ++x) {
          if (std::max({std::abs(x), std::abs(y), std::abs(z)}) != ring)
            continue;
          offsets.push_back(make_int3(x, y, z));
          distances.push_back(lowerBound);
        }
      }
    }
  }

  if (!cellOffsets.allocate(offsets.size()) ||
      !cellOffsetDistances.allocate(distances.size()) ||
      !cellOffsets.uploadAsync(offsets.data(), offsets.size(), stream) ||
      !cellOffsetDistances.uploadAsync(distances.data(), distances.size(),
                                       stream) ||
      !cudaOk(cudaStreamSynchronize(stream),
              "synchronize KNN cell offset uploads"))
    return false;
  cellOffsetCount = uint32_t(offsets.size());
  return true;
}

bool KNNNeighbors::Implementation::buildGrid(
    const std::vector<glm::vec4> &seedsIn) {
  std::vector<uint32_t> hostCounts(grid.totalCells, 0u);
  std::vector<uint32_t> hostCellIds(seedCountValue);

  for (uint32_t seed = 0; seed < seedCountValue; ++seed) {
    const glm::vec4 &point = seedsIn[seed];
    const int3 cellIndex =
        cellIndexOf(make_float3(point.x, point.y, point.z), grid);
    const uint32_t cell =
        linearCell(cellIndex.x, cellIndex.y, cellIndex.z, grid);

    hostCellIds[seed] = cell;

    ++hostCounts[cell];
  }

  std::vector<uint32_t> hostStarts(size_t(grid.totalCells) + 1u, 0u);
  for (uint32_t cell = 0; cell < grid.totalCells; ++cell) {
    hostStarts[cell + 1] = hostStarts[cell] + hostCounts[cell];
  }
  if (hostStarts.back() != seedCountValue) {
    std::cerr << "[KNN] grid histogram lost seeds: " << hostStarts.back() << "/"
              << seedCountValue << std::endl;
    return false;
  }

  std::vector<uint32_t> hostCursor(hostStarts.begin(), hostStarts.end() - 1);
  std::vector<BucketEntry> hostEntries(seedCountValue);
  for (uint32_t seed = 0; seed < seedCountValue; ++seed) {
    BucketEntry entry{};
    entry.position = make_float4(seedsIn[seed].x, seedsIn[seed].y,
                                 seedsIn[seed].z, seedsIn[seed].w);
    entry.id = seed;
    hostEntries[hostCursor[hostCellIds[seed]]++] = entry;
  }

  if (!cellStart.uploadAsync(hostStarts.data(), hostStarts.size(), stream) ||
      !bucketEntries.uploadAsync(hostEntries.data(), hostEntries.size(),
                                 stream) ||
      !cudaOk(cudaStreamSynchronize(stream), "synchronize KNN grid uploads"))
    return false;
  return true;
}

DeviceKnn KNNNeighbors::Implementation::makeDeviceKnn() const {
  return {seeds.get(),       seedCountValue,
          cellStart.get(),   bucketEntries.get(),
          cellOffsets.get(), cellOffsetDistances.get(),
          cellOffsetCount,   grid};
}

bool KNNNeighbors::Implementation::buildNeighbors(
    uint32_t k, const std::vector<uint32_t> &queryIds) {
  const DeviceKnn knn = makeDeviceKnn();
  DeviceBuffer<uint32_t> deviceQueryIds;
  if (!deviceQueryIds.allocate(queryIds.size()) ||
      !deviceQueryIds.uploadAsync(queryIds.data(), queryIds.size(), stream))
    return false;
  if (!cudaOk(cudaMemsetAsync(searchFailure.get(), 0, sizeof(uint32_t), stream),
              "clear KNN search status"))
    return false;

  if (!launchKnnChunks(knn, neighbors.get(), distanceHeap.get(),
                       searchFailure.get(), deviceQueryIds.get(),
                       uint32_t(queryIds.size()), k, 8192, neighborStrideValue,
                       true, stream))
    return false;

  uint32_t failed = 0;
  if (!cudaOk(cudaMemcpy(&failed, searchFailure.get(), sizeof(failed),
                         cudaMemcpyDeviceToHost),
              "read KNN search status"))
    return false;
  if (failed != 0) {
    std::cerr << "[KNN] exact shell search found too few neighbors"
              << std::endl;
    return false;
  }
  return true;
}

std::unique_ptr<KNNBatch::Implementation>
KNNNeighbors::Implementation::query(const uint32_t *deviceQueryIds,
                                    uint32_t count, uint32_t targetK) {
  if (!initialized || !deviceQueryIds || count == 0 || targetK == 0 ||
      targetK >= seedCountValue)
    return {};

  auto batch = std::make_unique<KNNBatch::Implementation>();
  batch->neighborStride = targetK;
  if (!batch->neighbors.allocate(size_t(count) * targetK))
    return {};

  const DeviceKnn knn = makeDeviceKnn();
  const size_t requiredDistanceHeap = size_t(std::min(count, 256u)) * targetK;
  if (distanceHeap.size() < requiredDistanceHeap &&
      !distanceHeap.allocate(requiredDistanceHeap))
    return {};
  if (!cudaOk(cudaMemsetAsync(searchFailure.get(), 0, sizeof(uint32_t), stream),
              "clear KNN search status"))
    return {};

  if (!launchKnnChunks(knn, batch->neighbors.get(), distanceHeap.get(),
                       searchFailure.get(), deviceQueryIds, count, targetK, 256,
                       targetK, false, stream))
    return {};

  uint32_t failed = 0;
  if (!cudaOk(cudaMemcpy(&failed, searchFailure.get(), sizeof(failed),
                         cudaMemcpyDeviceToHost),
              "read KNN search status"))
    return {};
  if (failed != 0) {
    std::cerr << "[KNN] exact shell search found too few query neighbors"
              << std::endl;
    return {};
  }
  return batch;
}

bool KNNNeighbors::Implementation::downloadPrefix(
    uint32_t maxNeighbors, std::vector<uint32_t> &out) const {
  if (!initialized || seedCountValue == 0 || maxNeighbors == 0 ||
      maxNeighbors > neighborStrideValue)
    return false;
  out.assign(size_t(seedCountValue) * maxNeighbors, InvalidNeighborId);
  if (!prefixBuffer.allocate(size_t(seedCountValue) * maxNeighbors))
    return false;

  const uint32_t blocks = (seedCountValue + KNNBlockSize - 1) / KNNBlockSize;
  gatherPrefixKernel<<<blocks, KNNBlockSize, 0, stream>>>(
      neighbors.get(), seedCountValue, neighborStrideValue, maxNeighbors,
      prefixBuffer.get());
  return cudaOk(cudaGetLastError(), "launch KNN prefix gather") &&
         cudaOk(cudaStreamSynchronize(stream),
                "synchronize KNN prefix gather") &&
         cudaOk(cudaMemcpy(out.data(), prefixBuffer.get(),
                           size_t(seedCountValue) * maxNeighbors *
                               sizeof(uint32_t),
                           cudaMemcpyDeviceToHost),
                "download KNN prefix");
}

const float *KNNNeighbors::Implementation::deviceSeeds() const {
  return reinterpret_cast<const float *>(seeds.get());
}

const uint32_t *KNNNeighbors::Implementation::deviceNeighborIds() const {
  return neighbors.get();
}

uint32_t KNNNeighbors::Implementation::neighborStride() const {
  return neighborStrideValue;
}

uint32_t KNNNeighbors::Implementation::seedCount() const {
  return seedCountValue;
}

KNNBatch::KNNBatch() = default;
KNNBatch::~KNNBatch() = default;
KNNBatch::KNNBatch(KNNBatch &&) noexcept = default;
KNNBatch &KNNBatch::operator=(KNNBatch &&) noexcept = default;

const uint32_t *KNNBatch::deviceNeighborIds() const {
  if (!implementation)
    return nullptr;
  return implementation->neighbors.get();
}

uint32_t KNNBatch::neighborStride() const {
  if (!implementation)
    return 0;
  return implementation->neighborStride;
}

KNNNeighbors::KNNNeighbors()
    : implementation(std::make_unique<Implementation>()) {}
KNNNeighbors::~KNNNeighbors() = default;
bool KNNNeighbors::initialize(VulkanDevice &vulkanDevice) {
  return implementation->initialize(vulkanDevice);
}
void KNNNeighbors::cleanup() { implementation->cleanup(); }
bool KNNNeighbors::build(const std::vector<glm::vec4> &seeds, uint32_t k,
                         const std::vector<uint32_t> &queryIds) {
  return implementation->build(seeds, k, queryIds);
}
bool KNNNeighbors::query(const uint32_t *deviceQueryIds, uint32_t count,
                         uint32_t k, KNNBatch &batch) {
  auto queried = implementation->query(deviceQueryIds, count, k);
  if (!queried)
    return false;
  batch.implementation.swap(queried);
  return true;
}
bool KNNNeighbors::downloadPrefix(uint32_t maxNeighbors,
                                  std::vector<uint32_t> &out) const {
  return implementation->downloadPrefix(maxNeighbors, out);
}
const float *KNNNeighbors::deviceSeeds() const {
  return implementation->deviceSeeds();
}
const uint32_t *KNNNeighbors::deviceNeighborIds() const {
  return implementation->deviceNeighborIds();
}
uint32_t KNNNeighbors::neighborStride() const {
  return implementation->neighborStride();
}
uint32_t KNNNeighbors::seedCount() const { return implementation->seedCount(); }

} // namespace voronoi
