#include "HeatGlobalSolverKernels.cuh"

#include <cuda_runtime.h>
#include <cmath>
#include <algorithm>

namespace heat::globalsolver {

static __device__ __forceinline__ float warpReduceSum(float val) {
    for (int offset = warpSize / 2; offset > 0; offset /= 2) {
        val += __shfl_down_sync(0xffffffff, val, offset);
    }
    return val;
}

static __device__ __forceinline__ float blockReduceSum(float val) {
    static __shared__ float shared[32];
    int lane = threadIdx.x % warpSize;
    int wid = threadIdx.x / warpSize;
    val = warpReduceSum(val);
    if (lane == 0) shared[wid] = val;
    __syncthreads();
    val = (threadIdx.x < (blockDim.x / warpSize)) ? shared[lane] : 0.0f;
    if (wid == 0) val = warpReduceSum(val);
    return val;
}

__global__ void gatherTemperaturesAndBuildRhsKernel(
    const float* __restrict__ sourceTemp,
    const uint32_t* __restrict__ localNodeIds,
    uint32_t nodeCount,
    uint32_t solverNodeOffset,
    const float* __restrict__ thermalMasses,
    float* __restrict__ x,
    float* __restrict__ rhs) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nodeCount) return;
    const uint32_t localId = localNodeIds[i];
    const uint32_t globalId = solverNodeOffset + i;
    const float t = sourceTemp[localId];
    x[globalId] = t;
    rhs[globalId] = thermalMasses[globalId] * t;
}

__global__ void addFixedContributionsKernel(
    uint32_t totalNodes,
    const heat::FixedRow* __restrict__ fixedRows,
    const heat::FixedContribution* __restrict__ fixedContributions,
    const float* __restrict__ boundaryTemperatures,
    float* __restrict__ rhs) {
    const uint32_t node = blockIdx.x * blockDim.x + threadIdx.x;
    if (node >= totalNodes) return;
    const heat::FixedRow row = fixedRows[node];
    float sum = 0.0f;
    for (uint32_t c = 0; c < row.contributionCount; ++c) {
        const heat::FixedContribution contrib = fixedContributions[row.contributionOffset + c];
        sum += contrib.coefficient * boundaryTemperatures[contrib.boundaryValueIndex];
    }
    rhs[node] += sum;
}

__global__ void computeInvDiagKernel(
    uint32_t totalNodes,
    const int* __restrict__ rowOffsets,
    const int* __restrict__ columnIndices,
    const float* __restrict__ values,
    float* __restrict__ invDiag) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= totalNodes) return;
    float diag = 1.0f;
    const int rowStart = rowOffsets[i];
    const int rowEnd = rowOffsets[i + 1];
    for (int j = rowStart; j < rowEnd; ++j) {
        if (columnIndices[j] == int(i)) {
            diag = values[j];
            break;
        }
    }
    invDiag[i] = (fabsf(diag) > 1e-12f) ? (1.0f / diag) : 1.0f;
}

__global__ void csrSpmvKernel(
    uint32_t totalNodes,
    const int* __restrict__ rowOffsets,
    const int* __restrict__ columnIndices,
    const float* __restrict__ values,
    const float* __restrict__ x,
    float* __restrict__ y,
    const PcgStatus* __restrict__ status) {
    if (status && status->converged) return;
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= totalNodes) return;
    float sum = 0.0f;
    const int rowStart = rowOffsets[i];
    const int rowEnd = rowOffsets[i + 1];
    for (int j = rowStart; j < rowEnd; ++j) {
        sum += values[j] * x[columnIndices[j]];
    }
    y[i] = sum;
}

__global__ void initResidualAndJacobiKernel(
    uint32_t totalNodes,
    const float* __restrict__ rhs,
    const float* __restrict__ Ap0,
    const float* __restrict__ invDiag,
    float* __restrict__ r,
    float* __restrict__ z,
    float* __restrict__ p,
    PcgStatus* __restrict__ status) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= totalNodes) return;
    const float ri = rhs[i] - Ap0[i];
    const float zi = ri * invDiag[i];
    r[i] = ri;
    z[i] = zi;
    p[i] = zi;
    if (i == 0 && status) {
        status->converged = 0;
        status->failed = 0;
        status->iterationCount = 0;
        status->rz = 0.0f;
        status->rzNew = 0.0f;
        status->pAp = 0.0f;
        status->residual0 = 0.0f;
        status->residual = 0.0f;
        status->alpha = 0.0f;
        status->beta = 0.0f;
    }
}

__global__ void reduceInitDotProductsKernel(
    uint32_t totalNodes,
    const float* __restrict__ r,
    const float* __restrict__ z,
    PcgStatus* __restrict__ status) {
    float sumRz = 0.0f;
    float sumR2 = 0.0f;
    for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < totalNodes; i += blockDim.x * gridDim.x) {
        const float ri = r[i];
        sumRz += ri * z[i];
        sumR2 += ri * ri;
    }
    sumRz = blockReduceSum(sumRz);
    sumR2 = blockReduceSum(sumR2);
    if (threadIdx.x == 0) {
        atomicAdd(&status->rz, sumRz);
        atomicAdd(&status->residual0, sumR2);
    }
}

__global__ void finalizeInitDotKernel(PcgStatus* __restrict__ status) {
    if (threadIdx.x == 0) {
        status->residual0 = sqrtf(max(status->residual0, 1e-24f));
        status->residual = status->residual0;
        if (status->residual0 < 1e-12f) {
            status->converged = 1;
        }
    }
}

__global__ void reducePApKernel(
    uint32_t totalNodes,
    const float* __restrict__ p,
    const float* __restrict__ Ap,
    PcgStatus* __restrict__ status) {
    if (status->converged) return;
    float sumPAp = 0.0f;
    for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < totalNodes; i += blockDim.x * gridDim.x) {
        sumPAp += p[i] * Ap[i];
    }
    sumPAp = blockReduceSum(sumPAp);
    if (threadIdx.x == 0) {
        atomicAdd(&status->pAp, sumPAp);
    }
}

__global__ void computeAlphaKernel(PcgStatus* __restrict__ status) {
    if (threadIdx.x == 0 && !status->converged) {
        const float denom = status->pAp;
        if (denom <= 1e-30f || !isfinite(denom)) {
            status->failed = 1;
            status->converged = 1; // stops remaining graph kernels
            status->alpha = 0.0f;
            return;
        }
        status->alpha = (fabsf(denom) > 1e-24f) ? (status->rz / denom) : 0.0f;
        status->rzNew = 0.0f;
        status->residual = 0.0f;
        status->pAp = 0.0f; // reset for next iteration
    }
}

__global__ void updateXAndRKernel(
    uint32_t totalNodes,
    const float* __restrict__ p,
    const float* __restrict__ Ap,
    const float* __restrict__ invDiag,
    float* __restrict__ x,
    float* __restrict__ r,
    float* __restrict__ z,
    PcgStatus* __restrict__ status) {
    if (status->converged) return;
    const float alpha = status->alpha;
    float localRz = 0.0f;
    float localR2 = 0.0f;
    for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < totalNodes; i += blockDim.x * gridDim.x) {
        const float xi = x[i] + alpha * p[i];
        const float ri = r[i] - alpha * Ap[i];
        const float zi = ri * invDiag[i];
        x[i] = xi;
        r[i] = ri;
        z[i] = zi;
        localRz += ri * zi;
        localR2 += ri * ri;
    }
    localRz = blockReduceSum(localRz);
    localR2 = blockReduceSum(localR2);
    if (threadIdx.x == 0) {
        atomicAdd(&status->rzNew, localRz);
        atomicAdd(&status->residual, localR2);
    }
}

__global__ void checkConvergenceAndComputeBetaKernel(
    PcgStatus* __restrict__ status,
    float tolerance,
    int iterIndex) {
    if (threadIdx.x == 0 && !status->converged) {
        status->iterationCount = iterIndex + 1;
        status->residual = sqrtf(max(status->residual, 0.0f));
        const float relRes = status->residual / max(status->residual0, 1e-12f);
        if (relRes <= tolerance) {
            status->converged = 1;
        } else {
            const float rzOld = max(status->rz, 1e-24f);
            status->beta = status->rzNew / rzOld;
            status->rz = status->rzNew;
        }
    }
}

__global__ void updatePKernel(
    uint32_t totalNodes,
    const float* __restrict__ z,
    float* __restrict__ p,
    const PcgStatus* __restrict__ status) {
    if (status->converged) return;
    const float beta = status->beta;
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= totalNodes) return;
    p[i] = z[i] + beta * p[i];
}

__global__ void scatterTemperaturesKernel(
    const float* __restrict__ x,
    uint32_t solverNodeOffset,
    const uint32_t* __restrict__ localNodeIds,
    uint32_t count,
    float* __restrict__ destination) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    destination[localNodeIds[i]] = x[solverNodeOffset + i];
}

} // namespace heat::globalsolver
