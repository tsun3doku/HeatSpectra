#include "HeatContactRuntime.hpp"

#include "HeatSourceRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"

const HeatSystemRuntime::SourceBinding* HeatContactRuntime::findSourceBindingByRuntimeModelId(
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    uint32_t runtimeModelId) const {
    if (runtimeModelId == 0) {
        return nullptr;
    }

    for (const HeatSystemRuntime::SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.geometryPackage.runtimeModelId == runtimeModelId && sourceBinding.heatSource) {
            return &sourceBinding;
        }
    }
    return nullptr;
}

uint32_t HeatContactRuntime::findReceiverIndexByRuntimeModelId(
    const std::vector<uint32_t>& receiverRuntimeModelIds,
    uint32_t runtimeModelId) const {
    if (runtimeModelId == 0) {
        return std::numeric_limits<uint32_t>::max();
    }

    for (uint32_t index = 0; index < receiverRuntimeModelIds.size(); ++index) {
        if (receiverRuntimeModelIds[index] == runtimeModelId) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

void HeatContactRuntime::clearCouplings(MemoryAllocator& memoryAllocator) {
    for (ContactCoupling& coupling : contactCouplings) {
        coupling.contactDescriptorsReady = false;
        coupling.contactComputeSetA = VK_NULL_HANDLE;
        coupling.contactComputeSetB = VK_NULL_HANDLE;
        coupling.source = nullptr;
        coupling.emitterReceiverIndex = std::numeric_limits<uint32_t>::max();
        coupling.receiverIndex = std::numeric_limits<uint32_t>::max();
        coupling.contactPairsCPU.clear();
        if (coupling.contactSampleBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactSampleBuffer, coupling.contactSampleBufferOffset);
            coupling.contactSampleBuffer = VK_NULL_HANDLE;
            coupling.contactSampleBufferOffset = 0;
        }
        if (coupling.contactCellMapBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactCellMapBuffer, coupling.contactCellMapBufferOffset);
            coupling.contactCellMapBuffer = VK_NULL_HANDLE;
            coupling.contactCellMapBufferOffset = 0;
        }
        if (coupling.contactCellRangeBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactCellRangeBuffer, coupling.contactCellRangeBufferOffset);
            coupling.contactCellRangeBuffer = VK_NULL_HANDLE;
            coupling.contactCellRangeBufferOffset = 0;
        }
        if (coupling.paramsBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.paramsBuffer, coupling.paramsBufferOffset);
            coupling.paramsBuffer = VK_NULL_HANDLE;
            coupling.paramsBufferOffset = 0;
        }
    }
    contactCouplings.clear();
}

void HeatContactRuntime::rebuildCouplings(
    MemoryAllocator& memoryAllocator,
    const std::vector<uint32_t>& receiverRuntimeModelIds,
    const std::vector<RuntimeContactResult>& resolvedContacts,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings) {
    clearCouplings(memoryAllocator);

    for (const RuntimeContactResult& resolvedContact : resolvedContacts) {
        const uint32_t receiverIndex = findReceiverIndexByRuntimeModelId(
            receiverRuntimeModelIds,
            resolvedContact.binding.receiverRuntimeModelId);
        if (receiverIndex == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        ContactCoupling coupling{};
        coupling.couplingType = resolvedContact.binding.contactPair.kind;
        coupling.emitterModelId = resolvedContact.binding.emitterRuntimeModelId;
        coupling.receiverModelId = resolvedContact.binding.receiverRuntimeModelId;
        coupling.receiverIndex = receiverIndex;
        coupling.params = HeatContactParams{};
        coupling.contactPairsCPU = resolvedContact.contactPairsCPU;

        if (resolvedContact.binding.contactPair.kind == ContactCouplingType::SourceToReceiver) {
            const HeatSystemRuntime::SourceBinding* sourceBinding =
                findSourceBindingByRuntimeModelId(sourceBindings, resolvedContact.binding.emitterRuntimeModelId);
            if (!sourceBinding) {
                continue;
            }
            coupling.source = sourceBinding->heatSource.get();
        } else {
            const uint32_t emitterReceiverIndex =
                findReceiverIndexByRuntimeModelId(receiverRuntimeModelIds, resolvedContact.binding.emitterRuntimeModelId);
            if (emitterReceiverIndex == std::numeric_limits<uint32_t>::max() ||
                emitterReceiverIndex == receiverIndex) {
                continue;
            }
            coupling.emitterReceiverIndex = emitterReceiverIndex;
        }

        contactCouplings.push_back(std::move(coupling));
    }
}
