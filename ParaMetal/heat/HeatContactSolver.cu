#define NOMINMAX
#include <Windows.h>

#include "HeatContactSolver.hpp"
#include "../cuda/CudaExternalBuffer.hpp"
#include "../cuda/CudaVulkanDevice.hpp"

#include "../vulkan/VulkanDevice.hpp"
#include "../vulkan/VulkanExternalBuffer.hpp"

#include <amgx_c.h>
#include <cuda_runtime.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace contactcuda {

struct DeviceModelNodes {
    float* temperatureA = nullptr;
    float* temperatureB = nullptr;
    uint32_t* localNodeIds = nullptr;
    uint32_t solverNodeOffset = 0;
    uint32_t nodeCount = 0;
};

struct DeviceFixedRow {
    uint32_t contributionOffset;
    uint32_t contributionCount;
};

struct DeviceFixedContribution {
    uint32_t boundaryValueIndex;
    float coefficient;
};

__global__ void gatherTemperatures(
    const float* source,
    const uint32_t* localNodeIds,
    uint32_t count,
    uint32_t destinationOffset,
    const float* thermalMasses,
    float* temperatures,
    float* rightHandSide) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const uint32_t solverNode = destinationOffset + index;
    const float temperature = source[localNodeIds[index]];
    temperatures[solverNode] = temperature;
    rightHandSide[solverNode] = thermalMasses[solverNode] * temperature;
}

__global__ void addFixedContributions(
    uint32_t nodeCount,
    const DeviceFixedRow* rows,
    const DeviceFixedContribution* contributions,
    const float* boundaryTemperatures,
    float* rightHandSide) {
    const uint32_t node = blockIdx.x * blockDim.x + threadIdx.x;
    if (node >= nodeCount) return;
    const DeviceFixedRow row = rows[node];
    float value = rightHandSide[node];
    for (uint32_t index = 0; index < row.contributionCount; ++index) {
        const DeviceFixedContribution contribution = contributions[row.contributionOffset + index];
        value += contribution.coefficient * boundaryTemperatures[contribution.boundaryValueIndex];
    }
    rightHandSide[node] = value;
}

__global__ void scatterTemperatures(
    const float* temperatures,
    uint32_t sourceOffset,
    const uint32_t* localNodeIds,
    uint32_t count,
    float* destination) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    destination[localNodeIds[index]] = temperatures[sourceOffset + index];
}

bool amgxOk(AMGX_RC result, const char* operation) {
    if (result == AMGX_RC_OK) return true;
    char error[1024]{};
    AMGX_get_error_string(result, error, sizeof(error));
    std::cerr << "[AmgX] " << operation << " failed: " << error << std::endl;
    return false;
}

bool cudaOk(cudaError_t result, const char* operation) {
    if (result == cudaSuccess) return true;
    std::cerr << "[AmgX] " << operation << " failed: " << cudaGetErrorString(result) << std::endl;
    return false;
}

void finalizeAmgxLibrary() {
    AMGX_finalize();
}

bool initializeAmgxLibrary() {
    static const bool initialized = []() {
        if (!amgxOk(AMGX_initialize(), "initialize library")) return false;
        std::atexit(finalizeAmgxLibrary);
        return true;
    }();
    return initialized;
}

} // namespace contactcuda

class HeatContactSolver::Implementation {
public:
    ~Implementation() { cleanup(); }

    bool initialize(
        VulkanDevice& vulkanDevice,
        const std::vector<int>& rowOffsets,
        const std::vector<int>& columnIndices,
        const std::vector<float>& values,
        const std::vector<float>& masses,
        const std::vector<ModelNodes>& inputModels,
        const std::vector<FixedRow>& inputFixedRows,
        const std::vector<FixedContribution>& inputFixedContributions,
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
        if (cudaDevice < 0 || !contactcuda::cudaOk(cudaSetDevice(cudaDevice), "cudaSetDevice")) return false;

        if (!createTimelineSemaphore()) return false;
        if (!importTimelineSemaphore()) return false;

        if (!contactcuda::cudaOk(cudaMalloc(&deviceTemperatures, masses.size() * sizeof(float)), "allocate temperatures") ||
            !contactcuda::cudaOk(cudaMalloc(&deviceRightHandSide, masses.size() * sizeof(float)), "allocate RHS") ||
            !contactcuda::cudaOk(cudaMalloc(&deviceThermalMasses, masses.size() * sizeof(float)), "allocate masses") ||
            !contactcuda::cudaOk(cudaMemcpy(deviceThermalMasses, masses.data(), masses.size() * sizeof(float), cudaMemcpyHostToDevice), "upload masses")) {
            return false;
        }

        if (!inputFixedRows.empty()) {
            if (!contactcuda::cudaOk(cudaMalloc(&deviceFixedRows, inputFixedRows.size() * sizeof(contactcuda::DeviceFixedRow)), "allocate fixed rows") ||
                !contactcuda::cudaOk(cudaMemcpy(deviceFixedRows, inputFixedRows.data(), inputFixedRows.size() * sizeof(contactcuda::DeviceFixedRow), cudaMemcpyHostToDevice), "upload fixed rows")) {
                return false;
            }
        }
        if (!inputFixedContributions.empty()) {
            if (!contactcuda::cudaOk(cudaMalloc(&deviceFixedContributions, inputFixedContributions.size() * sizeof(contactcuda::DeviceFixedContribution)), "allocate fixed contributions") ||
                !contactcuda::cudaOk(cudaMemcpy(deviceFixedContributions, inputFixedContributions.data(), inputFixedContributions.size() * sizeof(contactcuda::DeviceFixedContribution), cudaMemcpyHostToDevice), "upload fixed contributions")) {
                return false;
            }
        }
        if (boundaryValueCount > 0 &&
            !contactcuda::cudaOk(cudaMalloc(&deviceBoundaryTemperatures, boundaryValueCount * sizeof(float)), "allocate boundary temperatures")) {
            return false;
        }

        models.reserve(inputModels.size());
        for (const ModelNodes& input : inputModels) {
            if (!input.externalA || !input.externalB || !input.temperatureA || !input.temperatureB || input.localNodeIds.empty()) return false;
            contactcuda::DeviceModelNodes model{};
            model.solverNodeOffset = input.solverNodeOffset;
            model.nodeCount = static_cast<uint32_t>(input.localNodeIds.size());
            if (!input.temperatureA->ensureMapped(*input.externalA, cudaDevice) ||
                !input.temperatureB->ensureMapped(*input.externalB, cudaDevice) ||
                !contactcuda::cudaOk(cudaMalloc(&model.localNodeIds, input.localNodeIds.size() * sizeof(uint32_t)), "allocate local node IDs") ||
                !contactcuda::cudaOk(cudaMemcpy(model.localNodeIds, input.localNodeIds.data(), input.localNodeIds.size() * sizeof(uint32_t), cudaMemcpyHostToDevice), "upload local node IDs")) {
                return false;
            }
            model.temperatureA = input.temperatureA->getPointer();
            model.temperatureB = input.temperatureB->getPointer();
            models.push_back(model);
        }

        if (!contactcuda::initializeAmgxLibrary()) return false;

        const char* configString =
            "config_version=2, determinism_flag=1, "
            "solver(main)=PCG, main:preconditioner(amg)=AMG, "
            "main:max_iters=30, main:tolerance=1e-6, main:convergence=RELATIVE_INI, "
            "main:norm=L2, main:monitor_residual=1, main:store_res_history=1, "
            "main:print_solve_stats=0, main:obtain_timings=0, "
            "amg:algorithm=AGGREGATION, amg:selector=SIZE_2, amg:interpolator=D2, "
            "amg:smoother(smooth)=BLOCK_JACOBI, smooth:relaxation_factor=0.8, "
            "amg:presweeps=0, amg:postsweeps=3, amg:cycle=V, amg:max_levels=50, "
            "amg:max_iters=1, amg:coarse_solver=NOSOLVER";

        if (!contactcuda::amgxOk(AMGX_config_create(&config, configString), "create config") ||
            !contactcuda::amgxOk(AMGX_resources_create(&resources, config, nullptr, 1, &cudaDevice), "create resources") ||
            !contactcuda::amgxOk(AMGX_matrix_create(&matrix, resources, AMGX_mode_dFFI), "create matrix") ||
            !contactcuda::amgxOk(AMGX_vector_create(&solution, resources, AMGX_mode_dFFI), "create solution") ||
            !contactcuda::amgxOk(AMGX_vector_create(&rightHandSide, resources, AMGX_mode_dFFI), "create RHS") ||
            !contactcuda::amgxOk(AMGX_solver_create(&solver, resources, AMGX_mode_dFFI, config), "create solver") ||
            !contactcuda::amgxOk(AMGX_matrix_upload_all(matrix, static_cast<int>(nodeCount),
                static_cast<int>(values.size()), 1, 1, rowOffsets.data(), columnIndices.data(),
                values.data(), nullptr), "upload matrix") ||
            !contactcuda::amgxOk(AMGX_vector_bind(solution, matrix), "bind solution") ||
            !contactcuda::amgxOk(AMGX_vector_bind(rightHandSide, matrix), "bind RHS") ||
            !contactcuda::amgxOk(AMGX_solver_setup(solver, matrix), "setup solver")) {
            return false;
        }

        initialized = true;
        return true;
    }

    bool solve(
        bool useA,
        const std::vector<float>& boundaryTemperatures,
        uint64_t waitValue,
        uint64_t signalValue) {
        if (!initialized) return false;
        cudaSetDevice(cudaDevice);

        if (waitValue > 0) {
            cudaExternalSemaphoreWaitParams waitParams{};
            waitParams.params.fence.value = waitValue;
            if (!contactcuda::cudaOk(cudaWaitExternalSemaphoresAsync(&cudaSemaphore, &waitParams, 1, nullptr), "wait Vulkan timeline")) return false;
        }

        constexpr uint32_t BlockSize = 256;
        for (const contactcuda::DeviceModelNodes& model : models) {
            const uint32_t blocks = (model.nodeCount + BlockSize - 1) / BlockSize;
            contactcuda::gatherTemperatures<<<blocks, BlockSize>>>(
                useA ? model.temperatureA : model.temperatureB,
                model.localNodeIds, model.nodeCount, model.solverNodeOffset,
                deviceThermalMasses, deviceTemperatures, deviceRightHandSide);
        }

        if (boundaryValueCount > 0) {
            if (boundaryTemperatures.size() != boundaryValueCount ||
                !contactcuda::cudaOk(cudaMemcpy(deviceBoundaryTemperatures, boundaryTemperatures.data(),
                    boundaryValueCount * sizeof(float), cudaMemcpyHostToDevice), "upload boundary temperatures")) {
                return false;
            }
            if (deviceFixedContributions) {
                const uint32_t blocks = (nodeCount + BlockSize - 1) / BlockSize;
                contactcuda::addFixedContributions<<<blocks, BlockSize>>>(
                    nodeCount, deviceFixedRows, deviceFixedContributions,
                    deviceBoundaryTemperatures, deviceRightHandSide);
            }
        }
        if (!contactcuda::cudaOk(cudaGetLastError(), "contact gather kernels")) return false;

        if (!contactcuda::amgxOk(AMGX_vector_upload(solution, static_cast<int>(nodeCount), 1, deviceTemperatures), "upload initial guess") ||
            !contactcuda::amgxOk(AMGX_vector_upload(rightHandSide, static_cast<int>(nodeCount), 1, deviceRightHandSide), "upload RHS")) {
            return false;
        }

        if (!contactcuda::amgxOk(AMGX_solver_solve(solver, rightHandSide, solution), "solve")) return false;
        if (!contactcuda::amgxOk(AMGX_vector_download(solution, deviceTemperatures), "download solution")) return false;

        for (const contactcuda::DeviceModelNodes& model : models) {
            const uint32_t blocks = (model.nodeCount + BlockSize - 1) / BlockSize;
            contactcuda::scatterTemperatures<<<blocks, BlockSize>>>(
                deviceTemperatures, model.solverNodeOffset, model.localNodeIds,
                model.nodeCount, useA ? model.temperatureA : model.temperatureB);
        }
        if (!contactcuda::cudaOk(cudaGetLastError(), "contact scatter kernels")) return false;

        cudaExternalSemaphoreSignalParams signalParams{};
        signalParams.params.fence.value = signalValue;
        if (!contactcuda::cudaOk(cudaSignalExternalSemaphoresAsync(&cudaSemaphore, &signalParams, 1, nullptr), "signal CUDA timeline")) {
            return false;
        }

        return true;
    }

    void cleanup() {
        if (cudaDevice >= 0) {
            cudaSetDevice(cudaDevice);
            cudaDeviceSynchronize();
        }
        if (solver) AMGX_solver_destroy(solver);
        if (rightHandSide) AMGX_vector_destroy(rightHandSide);
        if (solution) AMGX_vector_destroy(solution);
        if (matrix) AMGX_matrix_destroy(matrix);
        if (resources) AMGX_resources_destroy(resources);
        if (config) AMGX_config_destroy(config);
        solver = nullptr;
        rightHandSide = nullptr;
        solution = nullptr;
        matrix = nullptr;
        resources = nullptr;
        config = nullptr;

        for (size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex) {
            contactcuda::DeviceModelNodes& model = models[modelIndex];
            if (model.localNodeIds) cudaFree(model.localNodeIds);
        }
        models.clear();
        if (deviceBoundaryTemperatures) cudaFree(deviceBoundaryTemperatures);
        if (deviceFixedContributions) cudaFree(deviceFixedContributions);
        if (deviceFixedRows) cudaFree(deviceFixedRows);
        if (deviceThermalMasses) cudaFree(deviceThermalMasses);
        if (deviceRightHandSide) cudaFree(deviceRightHandSide);
        if (deviceTemperatures) cudaFree(deviceTemperatures);
        deviceBoundaryTemperatures = nullptr;
        deviceFixedContributions = nullptr;
        deviceFixedRows = nullptr;
        deviceThermalMasses = nullptr;
        deviceRightHandSide = nullptr;
        deviceTemperatures = nullptr;

        if (cudaSemaphore) {
            cudaDestroyExternalSemaphore(cudaSemaphore);
        }
        cudaSemaphore = nullptr;
        if (device != VK_NULL_HANDLE && timelineSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, timelineSemaphore, nullptr);
        }
        timelineSemaphore = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        nodeCount = 0;
        boundaryValueCount = 0;
        initialized = false;
    }

    bool createTimelineSemaphore() {
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
        return vkCreateSemaphore(device, &createInfo, nullptr, &timelineSemaphore) == VK_SUCCESS;
    }

    bool importTimelineSemaphore() {
        const auto getHandle = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
            vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
        if (!getHandle) return false;
        VkSemaphoreGetWin32HandleInfoKHR handleInfo{};
        handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        handleInfo.semaphore = timelineSemaphore;
        handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        HANDLE handle = nullptr;
        if (getHandle(device, &handleInfo, &handle) != VK_SUCCESS) return false;
        cudaExternalSemaphoreHandleDesc descriptor{};
        descriptor.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
        descriptor.handle.win32.handle = handle;
        const cudaError_t result = cudaImportExternalSemaphore(&cudaSemaphore, &descriptor);
        DWORD handleFlags = 0;
        if (GetHandleInformation(handle, &handleFlags)) CloseHandle(handle);
        return contactcuda::cudaOk(result, "import timeline semaphore");
    }

    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    cudaExternalSemaphore_t cudaSemaphore = nullptr;
    int cudaDevice = -1;

    AMGX_config_handle config = nullptr;
    AMGX_resources_handle resources = nullptr;
    AMGX_matrix_handle matrix = nullptr;
    AMGX_vector_handle solution = nullptr;
    AMGX_vector_handle rightHandSide = nullptr;
    AMGX_solver_handle solver = nullptr;
    float* deviceTemperatures = nullptr;
    float* deviceRightHandSide = nullptr;
    float* deviceThermalMasses = nullptr;
    float* deviceBoundaryTemperatures = nullptr;
    contactcuda::DeviceFixedRow* deviceFixedRows = nullptr;
    contactcuda::DeviceFixedContribution* deviceFixedContributions = nullptr;
    std::vector<contactcuda::DeviceModelNodes> models;
    uint32_t nodeCount = 0;
    uint32_t boundaryValueCount = 0;
    bool initialized = false;
};

HeatContactSolver::HeatContactSolver()
    : implementation(std::make_unique<Implementation>()) {
}

HeatContactSolver::~HeatContactSolver() = default;

bool HeatContactSolver::initialize(
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

bool HeatContactSolver::solve(
    bool temperatureBufferAIsCurrent,
    const std::vector<float>& boundaryTemperaturesC,
    uint64_t waitValue,
    uint64_t signalValue) {
    return implementation->solve(
        temperatureBufferAIsCurrent, boundaryTemperaturesC, waitValue, signalValue);
}

VkSemaphore HeatContactSolver::getTimelineSemaphore() const {
    return implementation->timelineSemaphore;
}

bool HeatContactSolver::isInitialized() const {
    return implementation->initialized;
}

void HeatContactSolver::cleanup() {
    implementation->cleanup();
}
