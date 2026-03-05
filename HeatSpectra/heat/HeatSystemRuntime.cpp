#include "HeatSystemRuntime.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "HeatReceiver.hpp"
#include "HeatSource.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "scene/Model.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <cstring>
#include <iostream>
#include <unordered_set>

HeatSystemRuntime::HeatSystemRuntime() = default;

HeatSystemRuntime::~HeatSystemRuntime() = default;

const HeatSystemRuntime::SourceCoupling* HeatSystemRuntime::findSourceCouplingForModel(const Model* model) const {
    if (!model) {
        return nullptr;
    }

    for (const SourceCoupling& sourceCoupling : sourceCouplings) {
        if (sourceCoupling.model == model) {
            return &sourceCoupling;
        }
    }

    return nullptr;
}

const HeatSystemRuntime::SourceCoupling* HeatSystemRuntime::findPrimarySourceCoupling() const {
    for (const SourceCoupling& sourceCoupling : sourceCouplings) {
        if (sourceCoupling.model && sourceCoupling.heatSource) {
            return &sourceCoupling;
        }
    }

    return nullptr;
}

Model* HeatSystemRuntime::findPrimaryReceiverModel() const {
    for (const auto& receiver : receivers) {
        if (receiver) {
            return &receiver->getModel();
        }
    }

    return nullptr;
}

void HeatSystemRuntime::clearContactCouplings(MemoryAllocator& memoryAllocator) {
    for (ContactCoupling& coupling : contactCouplings) {
        coupling.contactDescriptorsReady = false;
        coupling.contactComputeSetA = VK_NULL_HANDLE;
        coupling.contactComputeSetB = VK_NULL_HANDLE;
        coupling.source = nullptr;
        coupling.receiver = nullptr;
        if (coupling.contactPairBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactPairBuffer, coupling.contactPairBufferOffset);
            coupling.contactPairBuffer = VK_NULL_HANDLE;
            coupling.contactPairBufferOffset = 0;
        }
    }
    contactCouplings.clear();
}

bool HeatSystemRuntime::uploadContactPairsToCoupling(ContactCoupling& coupling, const std::vector<ContactPairGPU>& pairs, VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator, CommandPool& renderCommandPool) {
    VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    const std::size_t pairCount = pairs.empty() ? 1ull : pairs.size();
    VkDeviceSize bufferSize = sizeof(ContactPairGPU) * pairCount;

    coupling.contactDescriptorsReady = false;
    if (coupling.contactPairBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(coupling.contactPairBuffer, coupling.contactPairBufferOffset);
        coupling.contactPairBuffer = VK_NULL_HANDLE;
        coupling.contactPairBufferOffset = 0;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* stagingData = nullptr;
    if (createStagingBuffer(
        memoryAllocator,
        bufferSize,
        stagingBuffer,
        stagingOffset,
        &stagingData
    ) != VK_SUCCESS || !stagingData) {
        std::cerr << "[HeatSystemRuntime] Failed to create contact staging buffer" << std::endl;
        return false;
    }

    if (pairs.empty()) {
        std::memset(stagingData, 0, static_cast<std::size_t>(bufferSize));
    } else {
        std::memcpy(stagingData, pairs.data(), static_cast<std::size_t>(bufferSize));
    }

    auto [pairHandle, pairOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        storageAlignment
    );
    if (pairHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSystemRuntime] Failed to allocate contact pair buffer" << std::endl;
        memoryAllocator.free(stagingBuffer, stagingOffset);
        return false;
    }

    coupling.contactPairBuffer = pairHandle;
    coupling.contactPairBufferOffset = pairOffset;

    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy region{};
    region.srcOffset = stagingOffset;
    region.dstOffset = coupling.contactPairBufferOffset;
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, coupling.contactPairBuffer, 1, &region);
    renderCommandPool.endCommands(cmd);

    memoryAllocator.free(stagingBuffer, stagingOffset);
    return true;
}

void HeatSystemRuntime::initializeModelBindings(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager,
    Remesher& remesher, CommandPool& renderCommandPool, const std::vector<uint32_t>& activeSourceModelIds, const std::vector<uint32_t>& activeReceiverModelIds) {
    cleanupModelBindings(memoryAllocator);

    std::unordered_set<uint32_t> seenSourceIds;
    for (uint32_t sourceId : activeSourceModelIds) {
        if (sourceId == 0 || !seenSourceIds.insert(sourceId).second) {
            continue;
        }

        Model* sourceModel = resourceManager.getModelByID(sourceId);
        if (!sourceModel) {
            continue;
        }

        SourceCoupling sourceCoupling{};
        sourceCoupling.modelId = sourceId;
        sourceCoupling.model = sourceModel;
        sourceCoupling.heatSource = std::make_unique<HeatSource>(
            vulkanDevice,
            memoryAllocator,
            *sourceModel,
            remesher,
            renderCommandPool);
        if (!sourceCoupling.heatSource || !sourceCoupling.heatSource->isInitialized()) {
            std::cerr << "[HeatSystemRuntime] Failed to initialize heat source for model " << sourceId << std::endl;
            if (sourceCoupling.heatSource) {
                sourceCoupling.heatSource->cleanup();
            }
            continue;
        }
        sourceCouplings.push_back(std::move(sourceCoupling));
    }

    std::unordered_set<uint32_t> sourceIdSet;
    for (const SourceCoupling& sourceCoupling : sourceCouplings) {
        if (sourceCoupling.modelId != 0) {
            sourceIdSet.insert(sourceCoupling.modelId);
        }
    }

    std::unordered_set<uint32_t> seenReceiverIds;
    for (uint32_t receiverId : activeReceiverModelIds) {
        if (receiverId == 0 ||
            sourceIdSet.find(receiverId) != sourceIdSet.end() ||
            !seenReceiverIds.insert(receiverId).second) {
            continue;
        }

        Model* receiverModel = resourceManager.getModelByID(receiverId);
        if (!receiverModel) {
            continue;
        }

        addReceiver(vulkanDevice, memoryAllocator, remesher, renderCommandPool, receiverModel);
        receiverModelIds.push_back(receiverId);
    }
}

void HeatSystemRuntime::cleanupModelBindings(MemoryAllocator& memoryAllocator) {
    clearContactCouplings(memoryAllocator);

    for (SourceCoupling& sourceCoupling : sourceCouplings) {
        if (sourceCoupling.heatSource) {
            sourceCoupling.heatSource->cleanup();
        }
    }
    sourceCouplings.clear();

    for (auto& receiver : receivers) {
        if (receiver) {
            receiver->cleanup();
        }
    }
    receivers.clear();
    receiverModelIds.clear();
}

void HeatSystemRuntime::addReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Remesher& remesher, CommandPool& renderCommandPool, Model* model) {
    if (!model) {
        return;
    }

    auto receiver = std::make_unique<HeatReceiver>(vulkanDevice, memoryAllocator, *model, remesher, renderCommandPool);
    if (!receiver->createReceiverBuffers()) {
        std::cerr << "[HeatSystemRuntime] Failed to create receiver buffers for model " << model->getRuntimeModelId() << std::endl;
        receiver->cleanup();
        return;
    }
    if (!receiver->initializeReceiverBuffer()) {
        std::cerr << "[HeatSystemRuntime] Failed to initialize receiver staging buffer for model " << model->getRuntimeModelId() << std::endl;
        receiver->cleanup();
        return;
    }
    receivers.push_back(std::move(receiver));
}