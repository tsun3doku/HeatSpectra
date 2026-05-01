#pragma once

#include "HeatSystemRuntime.hpp"
#include "contact/ContactTypes.hpp"

#include <limits>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

class MemoryAllocator;
class VulkanDevice;
class HeatReceiverRuntime;

class HeatContactRuntime {
public:
    struct CouplingState {
        ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
        uint32_t emitterModelId = 0;
        uint32_t receiverModelId = 0;
        uint32_t emitterReceiverIndex = std::numeric_limits<uint32_t>::max();
        uint32_t receiverIndex = std::numeric_limits<uint32_t>::max();
        uint32_t affectedContactNodeCount = 0;
    };

    const std::vector<CouplingState>& getCouplings() const { return contactCouplings; }
    std::vector<CouplingState>& getCouplingsMutable() { return contactCouplings; }
    VkBuffer getContactConductanceBuffer() const { return contactConductanceBuffer; }
    VkDeviceSize getContactConductanceBufferOffset() const { return contactConductanceBufferOffset; }
    uint32_t getContactConductanceNodeCount() const { return contactConductanceNodeCount; }

    void setContactCouplings(
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        const std::vector<ContactCoupling>& contactCouplings);
    bool ensureCouplings(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        const std::unordered_map<uint32_t, uint32_t>& receiverVoronoiNodeOffsetByModelId,
        const std::unordered_map<uint32_t, std::vector<uint32_t>>& receiverVoronoiSeedFlagsByModelId,
        const std::unordered_map<uint32_t, std::vector<glm::vec3>>& receiverVoronoiSeedPositionsByModelId,
        float contactThermalConductance,
        bool forceRebuild,
        uint32_t totalVoronoiNodeCount);
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
        const SupportingHalfedge::IntrinsicMesh& receiverMesh,
        const SupportingHalfedge::IntrinsicMesh* emitterMesh,
        uint32_t receiverNodeOffset,
        uint32_t emitterNodeOffset,
        const std::vector<uint32_t>& receiverSeedFlags,
        const std::vector<glm::vec3>& receiverSeedPositions,
        const std::vector<uint32_t>* emitterSeedFlags,
        const std::vector<glm::vec3>* emitterSeedPositions,
        float contactThermalConductance,
        uint32_t totalVoronoiNodeCount,
        std::vector<float>& contactConductanceSum) const;

    std::vector<uint32_t> activeReceiverRuntimeModelIds;
    std::vector<ContactCoupling> activeContactCouplings;
    std::vector<CouplingState> contactCouplings;
    VkBuffer contactConductanceBuffer = VK_NULL_HANDLE;
    VkDeviceSize contactConductanceBufferOffset = 0;
    uint32_t contactConductanceNodeCount = 0;
    bool couplingsDirty = true;
};
