#include "KNNNeighborsKernels.cuh"

namespace voronoi {

static __device__ inline bool heapBetter(double distance, uint32_t id,
                                         double otherDistance, uint32_t otherId) {
    return distance < otherDistance ||
           (distance == otherDistance && id < otherId);
}

static __device__ inline bool heapWorse(double distance, uint32_t id,
                                        double otherDistance, uint32_t otherId) {
    return distance > otherDistance ||
           (distance == otherDistance && id > otherId);
}

static __device__ inline void siftUp(double* distances, uint32_t* ids, int index) {
    while (index > 0) {
        const int parent = (index - 1) >> 1;
        if (!heapWorse(distances[index], ids[index], distances[parent], ids[parent])) break;
        const double distance = distances[index];
        distances[index] = distances[parent];
        distances[parent] = distance;
        const uint32_t id = ids[index];
        ids[index] = ids[parent];
        ids[parent] = id;
        index = parent;
    }
}

static __device__ inline void siftDown(double* distances, uint32_t* ids, int size, int index) {
    for (;;) {
        const int left = index * 2 + 1;
        if (left >= size) return;
        const int right = left + 1;
        int worst = left;
        if (right < size && heapWorse(distances[right], ids[right],
                                      distances[left], ids[left])) {
            worst = right;
        }
        if (!heapWorse(distances[worst], ids[worst], distances[index], ids[index])) return;
        const double distance = distances[index];
        distances[index] = distances[worst];
        distances[worst] = distance;
        const uint32_t id = ids[index];
        ids[index] = ids[worst];
        ids[worst] = id;
        index = worst;
    }
}

static __device__ inline void insertCandidate(double* distances, uint32_t* ids, int& size,
                                               int capacity, double candidateDistance,
                                               uint32_t candidateId) {
    if (capacity <= 0) return;
    if (size < capacity) {
        distances[size] = candidateDistance;
        ids[size] = candidateId;
        ++size;
        siftUp(distances, ids, size - 1);
        return;
    }
    if (!heapBetter(candidateDistance, candidateId, distances[0], ids[0])) return;
    distances[0] = candidateDistance;
    ids[0] = candidateId;
    siftDown(distances, ids, size, 0);
}

static __device__ inline void sortCandidates(double* distances, uint32_t* ids, int size) {
    for (int end = size - 1; end > 0; --end) {
        const double distance = distances[0];
        distances[0] = distances[end];
        distances[end] = distance;
        const uint32_t id = ids[0];
        ids[0] = ids[end];
        ids[end] = id;
        siftDown(distances, ids, end, 0);
    }
}

static __device__ uint32_t queryExactKnn(const DeviceKnn& knn, uint32_t queryId,
                                         uint32_t k, double* distanceHeap,
                                         uint32_t* neighborIds) {
    const float4 queryPosition = knn.seeds[queryId];
    const float3 query = make_float3(queryPosition.x, queryPosition.y, queryPosition.z);
    const int3 queryCell = cellIndexOf(query, knn.grid);
    int neighborCount = 0;

    for (uint32_t offsetIndex = 0; offsetIndex < knn.cellOffsetCount; ++offsetIndex) {
        if (neighborCount >= int(k) &&
            distanceHeap[0] < knn.cellOffsetDistances[offsetIndex]) {
            break;
        }

        const int3 offset = knn.cellOffsets[offsetIndex];
        const int x = queryCell.x + offset.x;
        const int y = queryCell.y + offset.y;
        const int z = queryCell.z + offset.z;
        if (x < 0 || x >= knn.grid.gridDim.x ||
            y < 0 || y >= knn.grid.gridDim.y ||
            z < 0 || z >= knn.grid.gridDim.z) continue;

        const uint32_t cell = linearCell(x, y, z, knn.grid);
        const uint32_t begin = min(knn.cellStart[cell], knn.seedCount);
        const uint32_t end = min(max(knn.cellStart[cell + 1], begin), knn.seedCount);
        for (uint32_t index = begin; index < end; ++index) {
            const BucketEntry entry = knn.bucketEntries[index];
            if (entry.id >= knn.seedCount || entry.id == queryId) continue;

            const double dx = double(query.x) - double(entry.position.x);
            const double dy = double(query.y) - double(entry.position.y);
            const double dz = double(query.z) - double(entry.position.z);
            insertCandidate(distanceHeap, neighborIds, neighborCount, int(k),
                            dx * dx + dy * dy + dz * dz, entry.id);
        }
    }

    sortCandidates(distanceHeap, neighborIds, neighborCount);
    return uint32_t(neighborCount);
}

__global__ void knnKernel(DeviceKnn knn, uint32_t* neighbors, double* distanceHeap,
                          uint32_t k, uint32_t* searchFailure,
                          const uint32_t* deviceQueryIds, uint32_t queryCount,
                          uint32_t outputBase,
                          uint32_t outputStride, bool scatterBySeedId) {
    const uint32_t localId = blockIdx.x * blockDim.x + threadIdx.x;
    if (localId >= queryCount) return;

    const uint32_t queryId = deviceQueryIds[localId];
    if (queryId >= knn.seedCount) {
        atomicExch(searchFailure, 1u);
        return;
    }

    double* queryDistanceHeap = distanceHeap + size_t(localId) * k;
    const uint32_t outputRow = scatterBySeedId ? queryId : outputBase + localId;
    uint32_t* outputIds = neighbors + size_t(outputRow) * outputStride;
    const uint32_t neighborCount = queryExactKnn(
        knn, queryId, k, queryDistanceHeap, outputIds);
    if (neighborCount < k) {
        atomicExch(searchFailure, 1u);
    }
    for (uint32_t index = neighborCount; index < k; ++index) {
        outputIds[index] = InvalidNeighborId;
    }
}

__global__ void gatherPrefixKernel(const uint32_t* neighbors, uint32_t seedCount,
                                   uint32_t neighborStride, uint32_t maxNeighbors,
                                   uint32_t* out) {
    const uint32_t id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id >= seedCount) return;
    const uint32_t* row = neighbors + size_t(id) * neighborStride;
    uint32_t* destination = out + size_t(id) * maxNeighbors;
    for (uint32_t index = 0; index < maxNeighbors; ++index) {
        destination[index] = row[index];
    }
}

} // namespace voronoi
