#pragma once

#include "HeatGpuStructs.hpp"

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanDevice;
class VulkanExternalBuffer;
class CudaExternalBuffer;

class HeatGlobalSolver {
public:
    using FixedContribution = heat::FixedContribution;
    using FixedRow = heat::FixedRow;

    struct ModelNodes {
        uint32_t solverNodeOffset = 0;
        std::vector<uint32_t> localNodeIds;
        const VulkanExternalBuffer* externalA = nullptr;
        const VulkanExternalBuffer* externalB = nullptr;
        CudaExternalBuffer* temperatureA = nullptr;
        CudaExternalBuffer* temperatureB = nullptr;
    };

    HeatGlobalSolver();
    ~HeatGlobalSolver();

    HeatGlobalSolver(const HeatGlobalSolver&) = delete;
    HeatGlobalSolver& operator=(const HeatGlobalSolver&) = delete;

    bool initialize(
        VulkanDevice& vulkanDevice,
        const std::vector<int>& rowOffsets,
        const std::vector<int>& columnIndices,
        const std::vector<float>& values,
        const std::vector<float>& thermalMasses,
        const std::vector<ModelNodes>& models,
        const std::vector<FixedRow>& fixedRows,
        const std::vector<FixedContribution>& fixedContributions,
        uint32_t boundaryValueCount);

    enum class SolvePollResult {
        Pending,
        Complete,
        Failed
    };

    bool launch(
        bool sourceIsA,
        const std::vector<float>& boundaryTemperaturesC,
        uint64_t destinationVulkanReleaseValue,
        uint64_t cudaReadyValue);

    SolvePollResult poll() const;
    void waitIdle();

    bool updateValues(
        const std::vector<float>& values,
        const std::vector<float>& thermalMasses,
        const std::vector<FixedRow>& fixedRows,
        const std::vector<FixedContribution>& fixedContributions,
        uint32_t boundaryValueCount);

    VkSemaphore getCudaReadySemaphore() const;
    VkSemaphore getVulkanReleaseSemaphore() const;
    bool hasSolveFailed() const;
    void cleanup();

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};
