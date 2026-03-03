#pragma once

#include "util/Structs.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

class CommandPool;
class HeatReceiver;
class HeatSource;
class MemoryAllocator;
class Model;
class Remesher;
class ResourceManager;
class VulkanDevice;

class HeatSystemRuntime {
public:
    HeatSystemRuntime();
    ~HeatSystemRuntime();

    struct SourceCoupling {
        uint32_t modelId = 0;
        Model* model = nullptr;
        std::unique_ptr<HeatSource> heatSource;
    };

    struct ContactCoupling {
        uint32_t sourceModelId = 0;
        uint32_t receiverModelId = 0;
        HeatSource* source = nullptr;
        HeatReceiver* receiver = nullptr;
        VkBuffer contactPairBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactPairBufferOffset = 0;
        VkDescriptorSet contactComputeSetA = VK_NULL_HANDLE;
        VkDescriptorSet contactComputeSetB = VK_NULL_HANDLE;
        bool contactDescriptorsReady = false;
    };

    const SourceCoupling* findSourceCouplingForModel(const Model* model) const;
    const SourceCoupling* findPrimarySourceCoupling() const;
    Model* findPrimaryReceiverModel() const;

    void clearContactCouplings(MemoryAllocator& memoryAllocator);
    bool uploadContactPairsToCoupling(
        ContactCoupling& coupling,
        const std::vector<ContactPairGPU>& pairs,
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool);

    void initializeModelBindings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ResourceManager& resourceManager,
        Remesher& remesher,
        CommandPool& renderCommandPool,
        const std::vector<uint32_t>& activeSourceModelIds,
        const std::vector<uint32_t>& activeReceiverModelIds);

    std::vector<SourceCoupling>& getSourceCouplingsMutable() { return sourceCouplings; }
    std::vector<std::unique_ptr<HeatReceiver>>& getReceiversMutable() { return receivers; }
    std::vector<ContactCoupling>& getContactCouplingsMutable() { return contactCouplings; }
    std::vector<uint32_t>& getReceiverModelIdsMutable() { return receiverModelIds; }

    void cleanupModelBindings(MemoryAllocator& memoryAllocator);

private:
    void addReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Remesher& remesher, CommandPool& renderCommandPool, Model* model);
    std::vector<SourceCoupling> sourceCouplings;
    std::vector<std::unique_ptr<HeatReceiver>> receivers;
    std::vector<ContactCoupling> contactCouplings;
    std::vector<uint32_t> receiverModelIds;
};
