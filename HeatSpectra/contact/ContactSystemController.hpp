#pragma once

#include "ContactSystem.hpp"
#include "ContactSystemRuntime.hpp"
#include "contact/ContactTypes.hpp"
#include "domain/ContactData.hpp"
#include "runtime/RuntimeContactTypes.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

class ComputeCache;
class MemoryAllocator;
class VulkanDevice;

class ContactSystemController {
public:
    struct Config {
        RuntimeContactBinding binding{};
    };

    struct HandleInfo {
        uint64_t key = 0;
        uint64_t revision = 0;
        uint32_t count = 0;
        bool hasContact = false;
    };

    ContactSystemController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ComputeCache& computeCache);
    ~ContactSystemController();

    void configure(const Config& config);
    void disable();
    bool exportProduct(ContactProduct& outProduct) const;
    const ContactSystemRuntime& getRuntime() const { return runtime; }

    bool computePreviewForRuntimePair(
        const RuntimeContactPairConfig& pair,
        ContactSystem::Result& outPreview,
        HandleInfo& outHandle,
        bool forceRebuild,
        bool& outPreviewUpdated);

    bool computePairs(
        const RuntimeContactPairConfig& pair,
        std::vector<ContactPair>& outPairs,
        bool forceRebuild = false);

    void clearCache();

private:
    static uint64_t buildCacheKey(const RuntimeContactPairConfig& pair);
    static uint64_t buildDomainKey();

    ContactSystem contactSystem;
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ComputeCache& computeCache;
    ContactSystemRuntime runtime;
};
