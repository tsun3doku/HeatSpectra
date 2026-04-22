#pragma once

#include "HeatContactParams.hpp"
#include "HeatSystemRuntime.hpp"
#include "contact/ContactTypes.hpp"

#include <limits>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

class MemoryAllocator;
class VulkanDevice;

class HeatContactRuntime {
public:
    struct CouplingState {
        ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
        uint32_t emitterModelId = 0;
        uint32_t receiverModelId = 0;
        uint32_t emitterReceiverIndex = std::numeric_limits<uint32_t>::max();
        uint32_t receiverIndex = std::numeric_limits<uint32_t>::max();
        VkBuffer contactSampleBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactSampleBufferOffset = 0;
        uint32_t contactSampleCount = 0;
        VkBuffer contactCellMapBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactCellMapBufferOffset = 0;
        uint32_t contactCellMapCount = 0;
        VkBuffer contactCellRangeBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactCellRangeBufferOffset = 0;
        uint32_t contactCellRangeCount = 0;
        VkBuffer emitterTriangleIndexBuffer = VK_NULL_HANDLE;
        VkDeviceSize emitterTriangleIndexBufferOffset = 0;
        VkBuffer emitterVoronoiMappingBuffer = VK_NULL_HANDLE;
        VkDeviceSize emitterVoronoiMappingBufferOffset = 0;
        HeatContactParams params{};
        VkBuffer paramsBuffer = VK_NULL_HANDLE;
        VkDeviceSize paramsBufferOffset = 0;
        VkDescriptorSet contactComputeSetA = VK_NULL_HANDLE;
        VkDescriptorSet contactComputeSetB = VK_NULL_HANDLE;
        bool contactDescriptorsReady = false;
    };

    const std::vector<CouplingState>& getCouplings() const { return contactCouplings; }
    std::vector<CouplingState>& getCouplingsMutable() { return contactCouplings; }

    void setContactCouplings(
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        const std::vector<ContactCoupling>& contactCouplings);
    bool ensureCouplings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        const std::unordered_map<uint32_t, std::vector<uint32_t>>& receiverSurfaceCellIndicesByModelId,
        const std::unordered_map<uint32_t, VkBuffer>& receiverSurfaceMappingBufferByModelId,
        const std::unordered_map<uint32_t, VkDeviceSize>& receiverSurfaceMappingBufferOffsetByModelId);
    bool needsRebuild() const { return couplingsDirty; }
    void clearCouplings(MemoryAllocator& memoryAllocator);

private:
    static bool areContactCouplingsEqual(
        const std::vector<ContactCoupling>& lhs,
        const std::vector<ContactCoupling>& rhs);
    const HeatSystemRuntime::SourceBinding* findSourceBindingByRuntimeModelId(
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        uint32_t runtimeModelId) const;
    uint32_t findReceiverIndexByRuntimeModelId(
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        uint32_t runtimeModelId) const;
    bool rebuildCouplingBuffers(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CouplingState& coupling,
        const ContactCoupling& contactCoupling,
        const std::vector<uint32_t>& receiverCellIndices,
        const std::unordered_map<uint32_t, VkBuffer>& receiverSurfaceMappingBufferByModelId,
        const std::unordered_map<uint32_t, VkDeviceSize>& receiverSurfaceMappingBufferOffsetByModelId) const;

    std::vector<uint32_t> activeReceiverRuntimeModelIds;
    std::vector<ContactCoupling> activeContactCouplings;
    std::vector<CouplingState> contactCouplings;
    bool couplingsDirty = true;
};
