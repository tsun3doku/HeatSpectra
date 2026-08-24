#include "GlobalContactRegion.cuh"
#include "GlobalContactRegionKernels.cuh"
#include "../../cuda/CudaBuffer.cuh"

#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <algorithm>

namespace heat {

using cudaUtils::CudaBuffer;

class GlobalContactRegion::Implementation {
public:
    Implementation() = default;
    ~Implementation() { cleanup(); }

    bool ensureCapacity(uint32_t requiredFaces) {
        if (requiredFaces <= maxFaces && d_cubTempStorage.get() != nullptr) {
            return true;
        }

        uint32_t newMaxFaces = std::max(requiredFaces, maxFaces > 0 ? maxFaces * 2 : 1024u);
        cleanup();
        maxFaces = newMaxFaces;

        if (!d_keysIn.allocate(maxFaces) ||
            !d_keysOut.allocate(maxFaces) ||
            !d_faceIndicesIn.allocate(maxFaces) ||
            !d_faceIndicesOut.allocate(maxFaces) ||
            !d_flags.allocate(maxFaces) ||
            !d_regionStarts.allocate(maxFaces + 1) ||
            !d_actualRegionCount.allocate(1)) {
            cleanup();
            return false;
        }

        size_t sortBytes = 0;
        cub::DeviceRadixSort::SortPairs(
            nullptr, sortBytes,
            d_keysIn.get(), d_keysOut.get(),
            d_faceIndicesIn.get(), d_faceIndicesOut.get(),
            int(maxFaces));

        cub::CountingInputIterator<uint32_t> countingIterator(0);
        size_t selectBytes = 0;
        int dummyNum = 0;
        cub::DeviceSelect::Flagged(
            nullptr, selectBytes,
            countingIterator, d_flags.get(),
            d_regionStarts.get(), &dummyNum,
            int(maxFaces));

        size_t maxTempBytes = std::max(sortBytes, selectBytes);
        return d_cubTempStorage.allocate(maxTempBytes);
    }

    void cleanup() {
        d_keysIn.reset();
        d_keysOut.reset();
        d_faceIndicesIn.reset();
        d_faceIndicesOut.reset();
        d_flags.reset();
        d_regionStarts.reset();
        d_actualRegionCount.reset();
        d_cubTempStorage.reset();
        maxFaces = 0;
    }

    bool build(const ContactRegionBuildInput& input,
               const ContactRegionBuildOutput& output,
               cudaStream_t stream) {
        if (!output.d_regions || !output.d_indirectCmd) {
            return false;
        }

        if (input.faceCount == 0) {
            if (!ensureCapacity(1)) return false;
            cudaMemsetAsync(d_actualRegionCount.get(), 0, sizeof(uint32_t), stream);
            contactregion::writeIndirectCommandKernel<<<1, 1, 0, stream>>>(
                d_actualRegionCount.get(), output.d_indirectCmd);
            return true;
        }

        if (!ensureCapacity(input.faceCount)) {
            return false;
        }

        if (!input.d_faces || input.channelCount == 0 || !(input.tileSize > 0.0f) ||
            input.tileDim.x == 0 || input.tileDim.y == 0 || input.tileDim.z == 0) {
            return false;
        }

        uint32_t blockSize = 256;
        uint32_t gridDim = (input.faceCount + blockSize - 1) / blockSize;

        // 1. Generate spatial sorting keys for each contact face
        contactregion::generateRegionKeysKernel<<<gridDim, blockSize, 0, stream>>>(
            input.d_faces, input.faceCount, input.gridMin, input.tileSize, input.tileDim, input.channelCount,
            d_keysIn.get(), d_faceIndicesIn.get());

        // 2. Radix sort (key, faceIndex) pairs
        size_t sortBytes = d_cubTempStorage.size();
        cub::DeviceRadixSort::SortPairs(
            d_cubTempStorage.get(), sortBytes,
            d_keysIn.get(), d_keysOut.get(),
            d_faceIndicesIn.get(), d_faceIndicesOut.get(),
            int(input.faceCount), 0, sizeof(uint64_t) * 8, stream);

        // 3. Mark boundaries where adjacent sorted keys differ
        contactregion::markRegionBoundariesKernel<<<gridDim, blockSize, 0, stream>>>(
            d_keysOut.get(), input.faceCount, d_flags.get());

        // 4. Compact flagged boundary indices to form region start offsets
        cub::CountingInputIterator<uint32_t> countingIterator(0);
        size_t selectBytes = d_cubTempStorage.size();
        cub::DeviceSelect::Flagged(
            d_cubTempStorage.get(), selectBytes,
            countingIterator, d_flags.get(),
            d_regionStarts.get(),
            reinterpret_cast<int*>(d_actualRegionCount.get()),
            int(input.faceCount), stream);

        // 5. Finalize sentinel end index for the last region
        contactregion::finalizeRegionStartsKernel<<<1, 1, 0, stream>>>(
            d_actualRegionCount.get(), d_regionStarts.get(), input.faceCount);

        // 6. Reduce analytic contact disks in each region into oriented bounding boxes
        contactregion::reduceRegionBoundsKernel<<<input.faceCount, 256, 0, stream>>>(
            input.d_faces, d_faceIndicesOut.get(),
            d_regionStarts.get(), d_actualRegionCount.get(),
            input.channelCount, input.d_pairToPsiChannel,
            output.d_regions);

        // 7. Write indirect draw command for instanced bounding box rendering
        contactregion::writeIndirectCommandKernel<<<1, 1, 0, stream>>>(
            d_actualRegionCount.get(), output.d_indirectCmd);

        return true;
    }

private:
    uint32_t maxFaces = 0;
    CudaBuffer<uint64_t> d_keysIn, d_keysOut;
    CudaBuffer<uint32_t> d_faceIndicesIn, d_faceIndicesOut, d_flags, d_regionStarts;
    CudaBuffer<uint32_t> d_actualRegionCount;
    CudaBuffer<uint8_t> d_cubTempStorage;
};

GlobalContactRegion::GlobalContactRegion() : implementation(std::make_unique<Implementation>()) {}
GlobalContactRegion::~GlobalContactRegion() = default;

bool GlobalContactRegion::build(const ContactRegionBuildInput& input,
                                const ContactRegionBuildOutput& output,
                                cudaStream_t stream) {
    return implementation->build(input, output, stream);
}

void GlobalContactRegion::cleanup() {
    implementation->cleanup();
}

} // namespace heat
