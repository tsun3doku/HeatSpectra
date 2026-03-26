#pragma once

#include "HeatContactParams.hpp"
#include "HeatSystemRuntime.hpp"
#include "runtime/RuntimeContactTypes.hpp"

#include <limits>
#include <vector>

#include <vulkan/vulkan.h>

class HeatSourceRuntime;
class MemoryAllocator;

class HeatContactRuntime {
public:
    struct ContactCoupling {
        ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
        uint32_t emitterModelId = 0;
        uint32_t receiverModelId = 0;
        HeatSourceRuntime* source = nullptr;
        uint32_t emitterReceiverIndex = std::numeric_limits<uint32_t>::max();
        uint32_t receiverIndex = std::numeric_limits<uint32_t>::max();
        std::vector<ContactPair> contactPairsCPU;
        VkBuffer contactSampleBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactSampleBufferOffset = 0;
        uint32_t contactSampleCount = 0;
        VkBuffer contactCellMapBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactCellMapBufferOffset = 0;
        uint32_t contactCellMapCount = 0;
        VkBuffer contactCellRangeBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactCellRangeBufferOffset = 0;
        uint32_t contactCellRangeCount = 0;
        HeatContactParams params{};
        VkBuffer paramsBuffer = VK_NULL_HANDLE;
        VkDeviceSize paramsBufferOffset = 0;
        VkDescriptorSet contactComputeSetA = VK_NULL_HANDLE;
        VkDescriptorSet contactComputeSetB = VK_NULL_HANDLE;
        bool contactDescriptorsReady = false;
    };

    const std::vector<ContactCoupling>& getCouplings() const { return contactCouplings; }
    std::vector<ContactCoupling>& getCouplingsMutable() { return contactCouplings; }

    void clearCouplings(MemoryAllocator& memoryAllocator);
    void rebuildCouplings(
        MemoryAllocator& memoryAllocator,
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        const std::vector<RuntimeContactResult>& resolvedContacts,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings);

private:
    const HeatSystemRuntime::SourceBinding* findSourceBindingByRuntimeModelId(
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        uint32_t runtimeModelId) const;
    uint32_t findReceiverIndexByRuntimeModelId(
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        uint32_t runtimeModelId) const;

    std::vector<ContactCoupling> contactCouplings;
};
