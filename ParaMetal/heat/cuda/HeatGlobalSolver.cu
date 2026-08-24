#define NOMINMAX
#include <Windows.h>
#include "../HeatGlobalSolver.hpp"
#include "HeatGlobalSolverKernels.cuh"
#include "../../cuda/CudaExternalBuffer.hpp"
#include "../../cuda/CudaVulkanDevice.hpp"
#include "../../vulkan/VulkanDevice.hpp"
#include "../../vulkan/VulkanExternalBuffer.hpp"

#include <cuda_runtime.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

class HeatGlobalSolver::Implementation {
public:
    ~Implementation() { cleanup(); }

    bool initialize(
        VulkanDevice& vulkanDevice,
        const std::vector<int>& rowOffsets,
        const std::vector<int>& columnIndices,
        const std::vector<float>& values,
        const std::vector<float>& masses,
        const std::vector<ModelNodes>& inputModels,
        const std::vector<heat::FixedRow>& inputFixedRows,
        const std::vector<heat::FixedContribution>& inputFixedContributions,
        uint32_t inputBoundaryValueCount) {
        cleanup();
        if (masses.empty() || rowOffsets.size() != masses.size() + 1 ||
            columnIndices.size() != values.size() || inputModels.empty()) {
            return false;
        }

        device = vulkanDevice.getDevice();
        nodeCount = static_cast<uint32_t>(masses.size());
        boundaryValueCount = inputBoundaryValueCount;

        cudaDevice = cudaVulkan::findDevice(vulkanDevice.getPhysicalDevice());
        if (cudaDevice < 0 || !cudaOk(cudaSetDevice(cudaDevice), "cudaSetDevice")) return false;

        if (!cudaOk(cudaStreamCreateWithFlags(&solveStream, cudaStreamNonBlocking), "create solveStream") ||
            !cudaOk(cudaEventCreateWithFlags(&solveCompleteEvent, cudaEventDisableTiming), "create completeEvent")) {
            cleanup();
            return false;
        }

        if (!createTimelineSemaphore(cudaReadyVkSemaphore) ||
            !importTimelineSemaphore(cudaReadyVkSemaphore, cudaReadySemaphore) ||
            !createTimelineSemaphore(vulkanReleaseVkSemaphore) ||
            !importTimelineSemaphore(vulkanReleaseVkSemaphore, vulkanReleaseSemaphore)) {
            cleanup();
            return false;
        }

        // Import model CUDA external memory
        models.reserve(inputModels.size());
        for (const auto& inputModel : inputModels) {
            if (!inputModel.externalA || !inputModel.externalB || !inputModel.temperatureA || !inputModel.temperatureB) {
                cleanup();
                return false;
            }
            if (!inputModel.temperatureA->ensureMapped(*inputModel.externalA, cudaDevice) ||
                !inputModel.temperatureB->ensureMapped(*inputModel.externalB, cudaDevice)) {
                cleanup();
                return false;
            }

            DeviceModelNodes modelNode{};
            modelNode.d_temperatureA = inputModel.temperatureA->getPointer();
            modelNode.d_temperatureB = inputModel.temperatureB->getPointer();
            modelNode.solverNodeOffset = inputModel.solverNodeOffset;
            modelNode.nodeCount = static_cast<uint32_t>(inputModel.localNodeIds.size());

            if (modelNode.nodeCount > 0) {
                const size_t bytes = modelNode.nodeCount * sizeof(uint32_t);
                if (!cudaOk(cudaMalloc(&modelNode.d_localNodeIds, bytes), "alloc localNodeIds") ||
                    !cudaOk(cudaMemcpy(modelNode.d_localNodeIds, inputModel.localNodeIds.data(), bytes, cudaMemcpyHostToDevice), "upload localNodeIds")) {
                    cleanup();
                    return false;
                }
            }
            models.push_back(modelNode);
        }

        // Allocate CSR matrix and vector memory
        matrixValueCount = static_cast<uint32_t>(values.size());
        const size_t rowOffsetBytes = rowOffsets.size() * sizeof(int);
        const size_t colIndexBytes = columnIndices.size() * sizeof(int);
        const size_t valueBytes = values.size() * sizeof(float);
        const size_t nodeBytes = nodeCount * sizeof(float);

        if (!cudaOk(cudaMalloc(&d_rowOffsets, rowOffsetBytes), "alloc d_rowOffsets") ||
            !cudaOk(cudaMalloc(&d_columnIndices, colIndexBytes), "alloc d_columnIndices") ||
            !cudaOk(cudaMalloc(&d_values, valueBytes), "alloc d_values") ||
            !cudaOk(cudaMalloc(&d_invDiag, nodeBytes), "alloc d_invDiag") ||
            !cudaOk(cudaMalloc(&d_thermalMasses, nodeBytes), "alloc d_thermalMasses") ||
            !cudaOk(cudaMalloc(&d_rhs, nodeBytes), "alloc d_rhs") ||
            !cudaOk(cudaMalloc(&d_x, nodeBytes), "alloc d_x") ||
            !cudaOk(cudaMalloc(&d_r, nodeBytes), "alloc d_r") ||
            !cudaOk(cudaMalloc(&d_z, nodeBytes), "alloc d_z") ||
            !cudaOk(cudaMalloc(&d_p, nodeBytes), "alloc d_p") ||
            !cudaOk(cudaMalloc(&d_Ap, nodeBytes), "alloc d_Ap") ||
            !cudaOk(cudaMalloc(&d_status, sizeof(heat::globalsolver::PcgStatus)), "alloc d_status")) {
            cleanup();
            return false;
        }

        if (!cudaOk(cudaMemcpy(d_rowOffsets, rowOffsets.data(), rowOffsetBytes, cudaMemcpyHostToDevice), "upload d_rowOffsets") ||
            !cudaOk(cudaMemcpy(d_columnIndices, columnIndices.data(), colIndexBytes, cudaMemcpyHostToDevice), "upload d_columnIndices") ||
            !cudaOk(cudaMemcpy(d_values, values.data(), valueBytes, cudaMemcpyHostToDevice), "upload d_values") ||
            !cudaOk(cudaMemcpy(d_thermalMasses, masses.data(), nodeBytes, cudaMemcpyHostToDevice), "upload d_thermalMasses")) {
            cleanup();
            return false;
        }

        // Allocate dynamic capacity buffers
        bool pointersReallocated = false;
        if (!ensureDynamicCapacity(
                static_cast<uint32_t>(inputFixedRows.size()),
                static_cast<uint32_t>(inputFixedContributions.size()),
                boundaryValueCount,
                pointersReallocated)) {
            cleanup();
            return false;
        }

        if (!inputFixedRows.empty()) {
            const size_t bytes = inputFixedRows.size() * sizeof(heat::FixedRow);
            if (!cudaOk(cudaMemcpy(d_fixedRows, inputFixedRows.data(), bytes, cudaMemcpyHostToDevice), "upload d_fixedRows")) {
                cleanup();
                return false;
            }
        }

        if (!inputFixedContributions.empty()) {
            const size_t bytes = inputFixedContributions.size() * sizeof(heat::FixedContribution);
            if (!cudaOk(cudaMemcpy(d_fixedContributions, inputFixedContributions.data(), bytes, cudaMemcpyHostToDevice), "upload d_fixedContributions")) {
                cleanup();
                return false;
            }
        }

        // Compute initial Jacobi inverse diagonal
        const uint32_t gridNodes = (nodeCount + heat::globalsolver::BlockSize - 1) / heat::globalsolver::BlockSize;
        heat::globalsolver::computeInvDiagKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, solveStream>>>(
            nodeCount, d_rowOffsets, d_columnIndices, d_values, d_invDiag);
        if (!cudaOk(cudaStreamSynchronize(solveStream), "computeInvDiagKernel")) {
            cleanup();
            return false;
        }

        initialized = true;

        // Build execution graphs
        if (!buildGraphs()) {
            std::cerr << "[HeatGlobalSolver] Graph construction failed; using direct stream launch." << std::endl;
        }

        return true;
    }

    bool ensureDynamicCapacity(uint32_t rowCount, uint32_t contribCount, uint32_t bCount, bool& pointersReallocated) {
        pointersReallocated = false;
        if (rowCount > fixedRowCapacity) {
            if (d_fixedRows) cudaFree(d_fixedRows);
            d_fixedRows = nullptr;
            fixedRowCapacity = rowCount + 64;
            if (!cudaOk(cudaMalloc(&d_fixedRows, fixedRowCapacity * sizeof(heat::FixedRow)), "alloc d_fixedRows")) return false;
            pointersReallocated = true;
        }
        if (contribCount > fixedContributionCapacity) {
            if (d_fixedContributions) cudaFree(d_fixedContributions);
            d_fixedContributions = nullptr;
            fixedContributionCapacity = contribCount + 128;
            if (!cudaOk(cudaMalloc(&d_fixedContributions, fixedContributionCapacity * sizeof(heat::FixedContribution)), "alloc d_fixedContributions")) return false;
            pointersReallocated = true;
        }
        if (bCount > boundaryCapacity) {
            if (d_boundaryTemperatures) cudaFree(d_boundaryTemperatures);
            d_boundaryTemperatures = nullptr;
            boundaryCapacity = bCount + 16;
            if (!cudaOk(cudaMalloc(&d_boundaryTemperatures, boundaryCapacity * sizeof(float)), "alloc d_boundaryTemperatures")) return false;
            pointersReallocated = true;
        }
        if (bCount > pinnedBoundaryCapacity) {
            if (h_pinnedBoundary) cudaFreeHost(h_pinnedBoundary);
            h_pinnedBoundary = nullptr;
            pinnedBoundaryCapacity = bCount + 16;
            if (!cudaOk(cudaHostAlloc(&h_pinnedBoundary, pinnedBoundaryCapacity * sizeof(float), cudaHostAllocDefault), "alloc h_pinnedBoundary")) return false;
        }
        return true;
    }

    bool updateValues(
        const std::vector<float>& values,
        const std::vector<float>& masses,
        const std::vector<heat::FixedRow>& inputFixedRows,
        const std::vector<heat::FixedContribution>& inputFixedContributions,
        uint32_t inputBoundaryValueCount) {
        if (!initialized) return false;
        waitIdle();
        cudaSetDevice(cudaDevice);

        if (values.size() != matrixValueCount) return false;
        if (!cudaOk(cudaMemcpy(d_values, values.data(), values.size() * sizeof(float), cudaMemcpyHostToDevice), "update d_values")) {
            return false;
        }

        if (masses.size() == nodeCount) {
            if (!cudaOk(cudaMemcpyAsync(d_thermalMasses, masses.data(), nodeCount * sizeof(float), cudaMemcpyHostToDevice, solveStream), "upload d_thermalMasses")) {
                return false;
            }
        }

        const uint32_t gridNodes = (nodeCount + heat::globalsolver::BlockSize - 1) / heat::globalsolver::BlockSize;
        heat::globalsolver::computeInvDiagKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, solveStream>>>(
            nodeCount, d_rowOffsets, d_columnIndices, d_values, d_invDiag);

        bool pointersReallocated = false;
        if (!ensureDynamicCapacity(
                static_cast<uint32_t>(inputFixedRows.size()),
                static_cast<uint32_t>(inputFixedContributions.size()),
                inputBoundaryValueCount,
                pointersReallocated)) {
            return false;
        }

        boundaryValueCount = inputBoundaryValueCount;

        if (!inputFixedRows.empty()) {
            const size_t bytes = inputFixedRows.size() * sizeof(heat::FixedRow);
            if (!cudaOk(cudaMemcpyAsync(d_fixedRows, inputFixedRows.data(), bytes, cudaMemcpyHostToDevice, solveStream), "upload d_fixedRows")) {
                return false;
            }
        }
        if (!inputFixedContributions.empty()) {
            const size_t bytes = inputFixedContributions.size() * sizeof(heat::FixedContribution);
            if (!cudaOk(cudaMemcpyAsync(d_fixedContributions, inputFixedContributions.data(), bytes, cudaMemcpyHostToDevice, solveStream), "upload d_fixedContributions")) {
                return false;
            }
        }

        if (pointersReallocated) {
            destroyGraphs();
            if (!buildGraphs()) return false;
        }

        return cudaOk(cudaStreamSynchronize(solveStream), "sync updateValues");
    }

    bool launch(
        bool sourceIsA,
        const std::vector<float>& boundaryTemperatures,
        uint64_t destinationVulkanReleaseValue,
        uint64_t cudaReadyValue) {
        if (!initialized) return false;
        cudaSetDevice(cudaDevice);

        // Wait for Vulkan to finish reading the destination buffer before overwriting
        if (destinationVulkanReleaseValue > 0) {
            cudaExternalSemaphoreWaitParams waitParams{};
            waitParams.params.fence.value = destinationVulkanReleaseValue;
            if (!cudaOk(cudaWaitExternalSemaphoresAsync(&vulkanReleaseSemaphore, &waitParams, 1, solveStream), "wait Vulkan release")) {
                return false;
            }
        }

        // Upload boundary temperatures 
        if (boundaryValueCount > 0 && !boundaryTemperatures.empty() && h_pinnedBoundary && d_boundaryTemperatures) {
            const size_t count = std::min(size_t(boundaryValueCount), boundaryTemperatures.size());
            std::memcpy(h_pinnedBoundary, boundaryTemperatures.data(), count * sizeof(float));
            if (!cudaOk(cudaMemcpyAsync(
                    d_boundaryTemperatures, h_pinnedBoundary, count * sizeof(float),
                    cudaMemcpyHostToDevice, solveStream), "upload boundary temps")) {
                return false;
            }
        }

        // Launch the solver graph
        cudaGraphExec_t graphExec = sourceIsA ? graphExecAToB : graphExecBToA;
        if (graphExec) {
            if (!cudaOk(cudaGraphLaunch(graphExec, solveStream), "launch CUDA graph")) {
                return false;
            }
        } else {
            recordSolveSequence(sourceIsA, solveStream);
        }

        // Record completion polling event
        cudaEventRecord(solveCompleteEvent, solveStream);

        // Signal CUDA ready semaphore
        if (cudaReadyValue > 0) {
            cudaExternalSemaphoreSignalParams signalParams{};
            signalParams.params.fence.value = cudaReadyValue;
            if (!cudaOk(cudaSignalExternalSemaphoresAsync(&cudaReadySemaphore, &signalParams, 1, solveStream), "signal CUDA ready")) {
                return false;
            }
        }

        return true;
    }

    SolvePollResult poll() {
        if (!initialized || !solveCompleteEvent) return SolvePollResult::Complete;
        const cudaError_t status = cudaEventQuery(solveCompleteEvent);
        if (status == cudaSuccess) {
            return SolvePollResult::Complete;
        }
        if (status == cudaErrorNotReady) {
            return SolvePollResult::Pending;
        }
        std::cerr << "[HeatGlobalSolver] async solve failed: "
                  << cudaGetErrorString(status) << std::endl;
        return SolvePollResult::Failed;
    }

    void waitIdle() {
        if (solveStream) {
            cudaStreamSynchronize(solveStream);
        }
    }

    bool hasSolveFailed() const {
        if (!d_status) return false;
        heat::globalsolver::PcgStatus hostStatus{};
        if (cudaMemcpy(&hostStatus, d_status, sizeof(heat::globalsolver::PcgStatus), cudaMemcpyDeviceToHost) == cudaSuccess) {
            return hostStatus.failed != 0;
        }
        return false;
    }

    VkSemaphore getCudaReadySemaphore() const { return cudaReadyVkSemaphore; }
    VkSemaphore getVulkanReleaseSemaphore() const { return vulkanReleaseVkSemaphore; }

    void destroyGraphs() {
        if (graphExecAToB) { cudaGraphExecDestroy(graphExecAToB); graphExecAToB = nullptr; }
        if (graphExecBToA) { cudaGraphExecDestroy(graphExecBToA); graphExecBToA = nullptr; }
        if (graphAToB) { cudaGraphDestroy(graphAToB); graphAToB = nullptr; }
        if (graphBToA) { cudaGraphDestroy(graphBToA); graphBToA = nullptr; }
    }

    void cleanup() {
        waitIdle();
        destroyGraphs();
        if (solveCompleteEvent) { cudaEventDestroy(solveCompleteEvent); solveCompleteEvent = nullptr; }
        if (solveStream) { cudaStreamDestroy(solveStream); solveStream = nullptr; }

        if (d_rowOffsets) { cudaFree(d_rowOffsets); d_rowOffsets = nullptr; }
        if (d_columnIndices) { cudaFree(d_columnIndices); d_columnIndices = nullptr; }
        if (d_values) { cudaFree(d_values); d_values = nullptr; }
        if (d_invDiag) { cudaFree(d_invDiag); d_invDiag = nullptr; }
        if (d_thermalMasses) { cudaFree(d_thermalMasses); d_thermalMasses = nullptr; }
        if (d_rhs) { cudaFree(d_rhs); d_rhs = nullptr; }
        if (d_x) { cudaFree(d_x); d_x = nullptr; }
        if (d_r) { cudaFree(d_r); d_r = nullptr; }
        if (d_z) { cudaFree(d_z); d_z = nullptr; }
        if (d_p) { cudaFree(d_p); d_p = nullptr; }
        if (d_Ap) { cudaFree(d_Ap); d_Ap = nullptr; }
        if (d_status) { cudaFree(d_status); d_status = nullptr; }
        if (d_fixedRows) { cudaFree(d_fixedRows); d_fixedRows = nullptr; }
        if (d_fixedContributions) { cudaFree(d_fixedContributions); d_fixedContributions = nullptr; }
        if (d_boundaryTemperatures) { cudaFree(d_boundaryTemperatures); d_boundaryTemperatures = nullptr; }
        if (h_pinnedBoundary) { cudaFreeHost(h_pinnedBoundary); h_pinnedBoundary = nullptr; }

        fixedRowCapacity = 0;
        fixedContributionCapacity = 0;
        boundaryCapacity = 0;
        pinnedBoundaryCapacity = 0;

        for (auto& model : models) {
            if (model.d_localNodeIds) { cudaFree(model.d_localNodeIds); model.d_localNodeIds = nullptr; }
        }
        models.clear();

        if (cudaReadySemaphore) { cudaDestroyExternalSemaphore(cudaReadySemaphore); cudaReadySemaphore = nullptr; }
        if (vulkanReleaseSemaphore) { cudaDestroyExternalSemaphore(vulkanReleaseSemaphore); vulkanReleaseSemaphore = nullptr; }

        if (device != VK_NULL_HANDLE) {
            if (cudaReadyVkSemaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, cudaReadyVkSemaphore, nullptr);
                cudaReadyVkSemaphore = VK_NULL_HANDLE;
            }
            if (vulkanReleaseVkSemaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, vulkanReleaseVkSemaphore, nullptr);
                vulkanReleaseVkSemaphore = VK_NULL_HANDLE;
            }
        }

        device = VK_NULL_HANDLE;
        nodeCount = 0;
        matrixValueCount = 0;
        boundaryValueCount = 0;
        initialized = false;
    }

private:
    struct DeviceModelNodes {
        float* d_temperatureA = nullptr;
        float* d_temperatureB = nullptr;
        uint32_t* d_localNodeIds = nullptr;
        uint32_t solverNodeOffset = 0;
        uint32_t nodeCount = 0;
    };

    static bool cudaOk(cudaError_t result, const char* operation) {
        if (result == cudaSuccess) return true;
        std::cerr << "[HeatGlobalSolver] CUDA " << operation << " failed: " << cudaGetErrorString(result) << std::endl;
        return false;
    }

    void recordSolveSequence(bool sourceIsA, cudaStream_t stream) {
        const uint32_t gridNodes = (nodeCount + heat::globalsolver::BlockSize - 1) / heat::globalsolver::BlockSize;
        const uint32_t reduceBlocks = std::min(gridNodes, 64u);

        // Gather source temperature and build initial RHS
        for (const auto& model : models) {
            const uint32_t blocks = (model.nodeCount + heat::globalsolver::BlockSize - 1) / heat::globalsolver::BlockSize;
            heat::globalsolver::gatherTemperaturesAndBuildRhsKernel<<<blocks, heat::globalsolver::BlockSize, 0, stream>>>(
                sourceIsA ? model.d_temperatureA : model.d_temperatureB,
                model.d_localNodeIds, model.nodeCount, model.solverNodeOffset,
                d_thermalMasses, d_x, d_rhs);
        }

        // Add fixed boundary contributions
        if (boundaryValueCount > 0 && d_fixedContributions && d_fixedRows) {
            heat::globalsolver::addFixedContributionsKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, stream>>>(
                nodeCount, d_fixedRows, d_fixedContributions, d_boundaryTemperatures, d_rhs);
        }

        // Compute initial Ap0 = A * x0
        heat::globalsolver::csrSpmvKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, stream>>>(
            nodeCount, d_rowOffsets, d_columnIndices, d_values, d_x, d_Ap, nullptr);

        // Initialize r = rhs - Ap0, z = invDiag * r, p = z
        heat::globalsolver::initResidualAndJacobiKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, stream>>>(
            nodeCount, d_rhs, d_Ap, d_invDiag, d_r, d_z, d_p, d_status);

        // Initial dot products: rz = r^T z, residual0 = ||r||_2
        heat::globalsolver::reduceInitDotProductsKernel<<<reduceBlocks, heat::globalsolver::BlockSize, 0, stream>>>(
            nodeCount, d_r, d_z, d_status);
        heat::globalsolver::finalizeInitDotKernel<<<1, 1, 0, stream>>>(d_status);

        // PCG Iterations
        for (uint32_t iter = 0; iter < heat::globalsolver::MaxIterations; ++iter) {
            // Ap = A * p
            heat::globalsolver::csrSpmvKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, stream>>>(
                nodeCount, d_rowOffsets, d_columnIndices, d_values, d_p, d_Ap, d_status);

            // pAp = p^T * Ap
            heat::globalsolver::reducePApKernel<<<reduceBlocks, heat::globalsolver::BlockSize, 0, stream>>>(
                nodeCount, d_p, d_Ap, d_status);

            // alpha = rz / pAp
            heat::globalsolver::computeAlphaKernel<<<1, 1, 0, stream>>>(d_status);

            // x = x + alpha * p, r = r - alpha * Ap, z = invDiag * r, sum(rzNew), sum(r^2)
            heat::globalsolver::updateXAndRKernel<<<reduceBlocks, heat::globalsolver::BlockSize, 0, stream>>>(
                nodeCount, d_p, d_Ap, d_invDiag, d_x, d_r, d_z, d_status);

            // Check convergence & compute beta = rzNew / rz
            heat::globalsolver::checkConvergenceAndComputeBetaKernel<<<1, 1, 0, stream>>>(
                d_status, heat::globalsolver::ConvergenceTolerance, iter);

            // p = z + beta * p
            heat::globalsolver::updatePKernel<<<gridNodes, heat::globalsolver::BlockSize, 0, stream>>>(
                nodeCount, d_z, d_p, d_status);
        }

        // Scatter solution into alternate destination buffer
        for (const auto& model : models) {
            const uint32_t blocks = (model.nodeCount + heat::globalsolver::BlockSize - 1) / heat::globalsolver::BlockSize;
            heat::globalsolver::scatterTemperaturesKernel<<<blocks, heat::globalsolver::BlockSize, 0, stream>>>(
                d_x, model.solverNodeOffset, model.d_localNodeIds, model.nodeCount,
                sourceIsA ? model.d_temperatureB : model.d_temperatureA);
        }
    }

    bool buildGraphs() {
        if (!solveStream) return false;

        // Capture Graph A -> B
        if (!cudaOk(cudaStreamBeginCapture(solveStream, cudaStreamCaptureModeGlobal), "begin capture A->B")) return false;
        recordSolveSequence(true, solveStream);
        if (!cudaOk(cudaStreamEndCapture(solveStream, &graphAToB), "end capture A->B")) return false;
        if (!cudaOk(cudaGraphInstantiate(&graphExecAToB, graphAToB, nullptr, nullptr, 0), "instantiate graph A->B")) return false;

        // Capture Graph B -> A
        if (!cudaOk(cudaStreamBeginCapture(solveStream, cudaStreamCaptureModeGlobal), "begin capture B->A")) return false;
        recordSolveSequence(false, solveStream);
        if (!cudaOk(cudaStreamEndCapture(solveStream, &graphBToA), "end capture B->A")) return false;
        if (!cudaOk(cudaGraphInstantiate(&graphExecBToA, graphBToA, nullptr, nullptr, 0), "instantiate graph B->A")) return false;

        return true;
    }

    bool createTimelineSemaphore(VkSemaphore& sem) {
        VkExportSemaphoreCreateInfo exportInfo{};
        exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.pNext = &exportInfo;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0;
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &typeInfo;
        return vkCreateSemaphore(device, &createInfo, nullptr, &sem) == VK_SUCCESS;
    }

    bool importTimelineSemaphore(VkSemaphore vkSem, cudaExternalSemaphore_t& cudaSem) {
        const auto getHandle = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
            vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
        if (!getHandle) return false;
        VkSemaphoreGetWin32HandleInfoKHR handleInfo{};
        handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        handleInfo.semaphore = vkSem;
        handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        HANDLE handle = nullptr;
        if (getHandle(device, &handleInfo, &handle) != VK_SUCCESS) return false;
        cudaExternalSemaphoreHandleDesc descriptor{};
        descriptor.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
        descriptor.handle.win32.handle = handle;
        const cudaError_t result = cudaImportExternalSemaphore(&cudaSem, &descriptor);
        DWORD handleFlags = 0;
        if (GetHandleInformation(handle, &handleFlags)) CloseHandle(handle);
        return cudaOk(result, "import timeline semaphore");
    }

    VkDevice device = VK_NULL_HANDLE;
    int cudaDevice = -1;
    cudaStream_t solveStream = nullptr;
    cudaEvent_t solveCompleteEvent = nullptr;

    VkSemaphore cudaReadyVkSemaphore = VK_NULL_HANDLE;
    cudaExternalSemaphore_t cudaReadySemaphore = nullptr;

    VkSemaphore vulkanReleaseVkSemaphore = VK_NULL_HANDLE;
    cudaExternalSemaphore_t vulkanReleaseSemaphore = nullptr;

    cudaGraph_t graphAToB = nullptr;
    cudaGraphExec_t graphExecAToB = nullptr;
    cudaGraph_t graphBToA = nullptr;
    cudaGraphExec_t graphExecBToA = nullptr;

    int* d_rowOffsets = nullptr;
    int* d_columnIndices = nullptr;
    float* d_values = nullptr;
    float* d_invDiag = nullptr;
    float* d_thermalMasses = nullptr;
    float* d_rhs = nullptr;
    float* d_x = nullptr;
    float* d_r = nullptr;
    float* d_z = nullptr;
    float* d_p = nullptr;
    float* d_Ap = nullptr;
    heat::globalsolver::PcgStatus* d_status = nullptr;

    heat::FixedRow* d_fixedRows = nullptr;
    heat::FixedContribution* d_fixedContributions = nullptr;
    float* d_boundaryTemperatures = nullptr;
    float* h_pinnedBoundary = nullptr;

    uint32_t fixedRowCapacity = 0;
    uint32_t fixedContributionCapacity = 0;
    uint32_t boundaryCapacity = 0;
    uint32_t pinnedBoundaryCapacity = 0;

    std::vector<DeviceModelNodes> models;

    uint32_t nodeCount = 0;
    uint32_t matrixValueCount = 0;
    uint32_t boundaryValueCount = 0;
    bool initialized = false;
};

HeatGlobalSolver::HeatGlobalSolver() : implementation(std::make_unique<Implementation>()) {}
HeatGlobalSolver::~HeatGlobalSolver() = default;

bool HeatGlobalSolver::initialize(
    VulkanDevice& vulkanDevice,
    const std::vector<int>& rowOffsets,
    const std::vector<int>& columnIndices,
    const std::vector<float>& values,
    const std::vector<float>& thermalMasses,
    const std::vector<ModelNodes>& models,
    const std::vector<FixedRow>& fixedRows,
    const std::vector<FixedContribution>& fixedContributions,
    uint32_t boundaryValueCount) {
    return implementation->initialize(
        vulkanDevice, rowOffsets, columnIndices, values, thermalMasses,
        models, fixedRows, fixedContributions, boundaryValueCount);
}

bool HeatGlobalSolver::launch(
    bool sourceIsA,
    const std::vector<float>& boundaryTemperaturesC,
    uint64_t destinationVulkanReleaseValue,
    uint64_t cudaReadyValue) {
    return implementation->launch(
        sourceIsA, boundaryTemperaturesC, destinationVulkanReleaseValue, cudaReadyValue);
}

HeatGlobalSolver::SolvePollResult HeatGlobalSolver::poll() const {
    return implementation->poll();
}

void HeatGlobalSolver::waitIdle() {
    implementation->waitIdle();
}

bool HeatGlobalSolver::updateValues(
    const std::vector<float>& values,
    const std::vector<float>& thermalMasses,
    const std::vector<FixedRow>& fixedRows,
    const std::vector<FixedContribution>& fixedContributions,
    uint32_t boundaryValueCount) {
    return implementation->updateValues(values, thermalMasses, fixedRows, fixedContributions, boundaryValueCount);
}

VkSemaphore HeatGlobalSolver::getCudaReadySemaphore() const {
    return implementation->getCudaReadySemaphore();
}

VkSemaphore HeatGlobalSolver::getVulkanReleaseSemaphore() const {
    return implementation->getVulkanReleaseSemaphore();
}

bool HeatGlobalSolver::hasSolveFailed() const {
    return implementation->hasSolveFailed();
}

void HeatGlobalSolver::cleanup() {
    implementation->cleanup();
}
