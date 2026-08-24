#pragma once

#include "util/Units.hpp"

#include "heat/HeatGpuStructs.hpp"
#include "heat/HeatBoundaryRuntime.hpp"
#include "vulkan/VulkanExternalBuffer.hpp"
#include "cuda/CudaExternalBuffer.hpp"
#include "heat/HeatSystemPresets.hpp"
#include "voronoi/VoronoiNodeIndex.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>
#include <memory>

class HeatSystemPlayback;
class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class HeatModelRuntime {
public:
    HeatModelRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const std::vector<glm::vec3>& surfacePositions,
        const std::vector<glm::vec3>& surfaceNormals,
        const std::vector<uint32_t>& surfaceTriangleIndices,
        CommandPool& renderCommandPool,
        float initialTemperatureC);
    ~HeatModelRuntime();

    void setMaterialProperties(float density, float specificHeat, float conductivity);
    void setWorldUnit(units::LengthUnit unit) { worldUnit = unit; }
    float getMetersPerWorldUnit() const { return units::physics::metersPerWorldUnit(worldUnit); }
    void setInitialTemperatureC(float temperatureC) { initialTemperatureC = temperatureC; }
    void setBoundaryInputs(uint32_t conditionType, float temperatureC, float heatFlux,
        float heatTransferCoefficient, float volumetricPowerDensity) {
        boundaryConditionType = conditionType;
        boundaryTemperatureC = temperatureC;
        boundaryHeatFlux = heatFlux;
        boundaryHeatTransferCoefficient = heatTransferCoefficient;
        this->volumetricPowerDensity = volumetricPowerDensity;
    }

    void cleanup();
    bool isInitialized() const { return initialized; }

    float getDensity() const { return density; }
    float getSpecificHeat() const { return specificHeat; }
    float getConductivity() const { return conductivity; }
    float getInitialTemperatureC() const { return initialTemperatureC; }
    uint32_t getBoundaryConditionType() const { return boundaryConditionType; }
    const std::vector<uint32_t>& getDirichletNodeIds() const { return boundaryRuntime.getDirichletNodeIds(); }
    const std::vector<uint32_t>& getSurfaceNodeIds() const { return boundaryRuntime.getSurfaceNodeIds(); }
    const std::vector<float>& getSurfaceBoundaryAreas() const { return boundaryRuntime.getSurfaceBoundaryAreas(); }
    uint32_t getDirichletRegionId(uint32_t nodeId) const { return boundaryRuntime.getDirichletRegionId(nodeId); }
    bool getBoundaryRegionTemperatureC(uint32_t regionId, float& temperatureC) const {
        return boundaryRuntime.getRegionTemperatureC(regionId, temperatureC);
    }
    bool getBoundaryRegionAmbientTemperatureC(uint32_t regionId, float& temperatureC) const {
        return boundaryRuntime.getRegionAmbientTemperatureC(regionId, temperatureC);
    }
    bool getBoundaryRegionHeatFlux(uint32_t regionId, float& heatFlux) const {
        return boundaryRuntime.getRegionHeatFlux(regionId, heatFlux);
    }
    bool getBoundaryRegionHeatTransferCoefficient(uint32_t regionId, float& coefficient) const {
        return boundaryRuntime.getRegionHeatTransferCoefficient(regionId, coefficient);
    }
    float getVolumetricPowerDensity() const { return volumetricPowerDensity; }

    size_t getSurfaceVertexCount() const { return surfacePositions.size(); }

    void setGMLSSurfaceWeights(
        VkBuffer stencilBuffer,
        VkDeviceSize stencilBufferOffset,
        VkBuffer valueWeightBuffer,
        VkDeviceSize valueWeightBufferOffset,
        size_t valueWeightCount,
        VkBuffer gradientWeightBuffer,
        VkDeviceSize gradientWeightBufferOffset,
        size_t gradientWeightCount);

    bool updateAllDescriptors(
        VkBuffer surfaceBuffer,
        VkDeviceSize surfaceBufferOffset,
        VkBuffer surfaceGradientBuffer,
        VkDeviceSize surfaceGradientBufferOffset,
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorSetLayout gradientLayout,
        VkDescriptorPool surfacePool,
        VkBuffer playbackBuffer,
        VkDeviceSize playbackBufferOffset,
        bool forceReallocate = false);

    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }
    VkDescriptorSet getSurfaceGradientComputeSetA() const { return surfaceGradientComputeSetA; }
    VkDescriptorSet getSurfaceGradientComputeSetB() const { return surfaceGradientComputeSetB; }
    VkDescriptorSet getSurfaceHistoryComputeSetA() const { return surfaceHistoryComputeSetA; }
    VkDescriptorSet getSurfaceHistoryComputeSetB() const { return surfaceHistoryComputeSetB; }
    VkDescriptorSet getSurfaceGradientHistorySetA() const { return surfaceGradientHistorySetA; }
    VkDescriptorSet getSurfaceGradientHistorySetB() const { return surfaceGradientHistorySetB; }

    const std::vector<glm::vec3>& getSurfacePositions() const { return surfacePositions; }
    const std::vector<glm::vec3>& getSurfaceNormals() const { return surfaceNormals; }
    const std::vector<uint32_t>& getSurfaceTriangleIndices() const { return surfaceTriangleIndices; }

    void setNodePositions(const std::vector<glm::vec3>& nodePositions) { nodeIndex.rebuild(nodePositions); }
    const VoronoiNodeIndex& getNodeIndex() const { return nodeIndex; }
    void setHistoryBuffer(VkBuffer buffer, VkDeviceSize offset, uint32_t frameCapacity);

    void initializePlayback(VulkanDevice& device, MemoryAllocator& allocator, uint32_t frameCapacity);
    HeatSystemPlayback* getPlayback() const { return playback.get(); }

    bool ensureSimulationBuffers(uint32_t nodeCount);
    void cleanupSimulationBuffers();
    VkBuffer getTempBufferA() const { return tempBufferA.getBuffer(); }
    VkBuffer getTempBufferB() const { return tempBufferB.getBuffer(); }
    const VulkanExternalBuffer& getExternalTempBufferA() const { return tempBufferA; }
    const VulkanExternalBuffer& getExternalTempBufferB() const { return tempBufferB; }
    CudaExternalBuffer& getCudaTempBufferA() { return cudaTempBufferA; }
    CudaExternalBuffer& getCudaTempBufferB() { return cudaTempBufferB; }
    uint32_t getSimNodeCount() const { return simNodeCount; }

    const std::vector<float>& getNodalThermalMasses() const { return nodalThermalMasses; }
    void setNodalThermalMasses(std::vector<float> masses) { nodalThermalMasses = std::move(masses); }

    void updateHistoryDescriptorOffset(uint32_t displayFrame, VkDeviceSize frameStride, uint32_t currentFrame);
    bool configureBoundary(const std::vector<uint32_t>& nodeIds, const std::vector<float>& surfaceBoundaryAreas);
    bool buildBoundaryBuffers();
    bool configureVolumetricSource(float powerDensity);

    bool setRuntimeDirichletTemperatureC(uint32_t regionId, float temperatureC) {
        if (!boundaryRuntime.hasDirichletTemperature()) return false;
        boundaryTemperatureC = temperatureC;
        return boundaryRuntime.setDirichletTemperatureC(regionId, temperatureC);
    }
    bool setNeumannHeatFlux(uint32_t regionId, float heatFlux) {
        return boundaryRuntime.setNeumannHeatFlux(regionId, heatFlux);
    }
    bool setRobinState(uint32_t regionId, float ambientTemperatureC, float heatTransferCoefficient) {
        return boundaryRuntime.setRobinState(regionId, ambientTemperatureC, heatTransferCoefficient);
    }
    bool setRuntimeRobinTemperatureC(uint32_t regionId, float ambientTemperatureC) {
        boundaryTemperatureC = ambientTemperatureC;
        return boundaryRuntime.setRobinTemperatureC(regionId, ambientTemperatureC);
    }
    bool setVolumetricPowerDensity(float powerDensity);
    void uploadRuntimeLoads(VkCommandBuffer commandBuffer);

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    std::vector<glm::vec3> surfacePositions;
    std::vector<glm::vec3> surfaceNormals;
    std::vector<uint32_t> surfaceTriangleIndices;
    CommandPool& renderCommandPool;

    float density = HeatSimDefaults::density;
    float specificHeat = HeatSimDefaults::specificHeat;
    float conductivity = HeatSimDefaults::conductivity;
    units::LengthUnit worldUnit = units::defaultLengthUnit();
    float initialTemperatureC = HeatSimDefaults::ambientTemperatureC;
    uint32_t boundaryConditionType = 0;
    float boundaryTemperatureC = HeatSimDefaults::ambientTemperatureC;
    float boundaryHeatFlux = 0.0f;
    float boundaryHeatTransferCoefficient = 0.0f;
    float volumetricPowerDensity = 0.0f;
    VkBuffer gmlsSurfaceStencilBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceStencilBufferOffset = 0;
    VkBuffer gmlsSurfaceWeightBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceWeightBufferOffset = 0;
    size_t gmlsSurfaceWeightCount = 0;
    VkBuffer gmlsSurfaceGradientWeightBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceGradientWeightBufferOffset = 0;
    size_t gmlsSurfaceGradientWeightCount = 0;

    VkDescriptorSet surfaceComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceComputeSetB = VK_NULL_HANDLE;
    VkDescriptorSet surfaceGradientComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceGradientComputeSetB = VK_NULL_HANDLE;
    VkDescriptorSet surfaceHistoryComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceHistoryComputeSetB = VK_NULL_HANDLE;
    VkDescriptorSet surfaceGradientHistorySetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceGradientHistorySetB = VK_NULL_HANDLE;

    VoronoiNodeIndex nodeIndex;
    HeatBoundaryRuntime boundaryRuntime;
    std::vector<float> volumetricPowerDensities;
    bool volumetricPowerDensityDirty = false;
    VkBuffer volumetricPowerDensityBuffer = VK_NULL_HANDLE;
    VkDeviceSize volumetricPowerDensityBufferOffset = 0;
    VkBuffer volumetricPowerDensityStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize volumetricPowerDensityStagingBufferOffset = 0;
    void* volumetricPowerDensityStagingMapped = nullptr;

    VulkanExternalBuffer tempBufferA;
    VulkanExternalBuffer tempBufferB;
    CudaExternalBuffer cudaTempBufferA;
    CudaExternalBuffer cudaTempBufferB;
    VkBuffer historyBuffer = VK_NULL_HANDLE;
    VkDeviceSize historyBufferOffset = 0;
    uint32_t historyBufferFrameCapacity = 0;
    uint32_t simNodeCount = 0;
    std::vector<float> nodalThermalMasses;
    bool initialized = false;

    std::unique_ptr<HeatSystemPlayback> playback;
};
