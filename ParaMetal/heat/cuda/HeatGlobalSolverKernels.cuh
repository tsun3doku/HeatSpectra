#pragma once

#include "../HeatGpuStructs.hpp"

#include <cuda_runtime.h>
#include <cstdint>

namespace heat::globalsolver {

constexpr uint32_t BlockSize = 256;
constexpr uint32_t MaxIterations = 40;
constexpr float ConvergenceTolerance = 1e-6f;

struct PcgStatus {
    float rz;
    float rzNew;
    float pAp;
    float residual0;
    float residual;
    float alpha;
    float beta;
    int converged;
    int failed;
    int iterationCount;
};

__global__ void gatherTemperaturesAndBuildRhsKernel(
    const float* __restrict__ sourceTemp,
    const uint32_t* __restrict__ localNodeIds,
    uint32_t nodeCount,
    uint32_t solverNodeOffset,
    const float* __restrict__ thermalMasses,
    float* __restrict__ x,
    float* __restrict__ rhs);

__global__ void addFixedContributionsKernel(
    uint32_t totalNodes,
    const heat::FixedRow* __restrict__ fixedRows,
    const heat::FixedContribution* __restrict__ fixedContributions,
    const float* __restrict__ boundaryTemperatures,
    float* __restrict__ rhs);

__global__ void computeInvDiagKernel(
    uint32_t totalNodes,
    const int* __restrict__ rowOffsets,
    const int* __restrict__ columnIndices,
    const float* __restrict__ values,
    float* __restrict__ invDiag);

__global__ void csrSpmvKernel(
    uint32_t totalNodes,
    const int* __restrict__ rowOffsets,
    const int* __restrict__ columnIndices,
    const float* __restrict__ values,
    const float* __restrict__ x,
    float* __restrict__ y,
    const PcgStatus* __restrict__ status);

__global__ void initResidualAndJacobiKernel(
    uint32_t totalNodes,
    const float* __restrict__ rhs,
    const float* __restrict__ Ap0,
    const float* __restrict__ invDiag,
    float* __restrict__ r,
    float* __restrict__ z,
    float* __restrict__ p,
    PcgStatus* __restrict__ status);

__global__ void reduceInitDotProductsKernel(
    uint32_t totalNodes,
    const float* __restrict__ r,
    const float* __restrict__ z,
    PcgStatus* __restrict__ status);

__global__ void finalizeInitDotKernel(
    PcgStatus* __restrict__ status);

__global__ void reducePApKernel(
    uint32_t totalNodes,
    const float* __restrict__ p,
    const float* __restrict__ Ap,
    PcgStatus* __restrict__ status);

__global__ void computeAlphaKernel(
    PcgStatus* __restrict__ status);

__global__ void updateXAndRKernel(
    uint32_t totalNodes,
    const float* __restrict__ p,
    const float* __restrict__ Ap,
    const float* __restrict__ invDiag,
    float* __restrict__ x,
    float* __restrict__ r,
    float* __restrict__ z,
    PcgStatus* __restrict__ status);

__global__ void checkConvergenceAndComputeBetaKernel(
    PcgStatus* __restrict__ status,
    float tolerance,
    int iterIndex);

__global__ void updatePKernel(
    uint32_t totalNodes,
    const float* __restrict__ z,
    float* __restrict__ p,
    const PcgStatus* __restrict__ status);

__global__ void scatterTemperaturesKernel(
    const float* __restrict__ x,
    uint32_t solverNodeOffset,
    const uint32_t* __restrict__ localNodeIds,
    uint32_t count,
    float* __restrict__ destination);

} // namespace heat::globalsolver
