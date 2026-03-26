#pragma once

#include "ContactSystem.hpp"
#include "ContactSystemRuntime.hpp"
#include "contact/ContactTypes.hpp"
#include "domain/ContactData.hpp"
#include "runtime/RuntimePackages.hpp"

#include <cstdint>
#include <vector>

class ComputeCache;
class NodePayloadRegistry;

class ContactSystemController {
public:
    struct HandleInfo {
        uint64_t key = 0;
        uint64_t revision = 0;
        uint32_t count = 0;
        bool hasContact = false;
    };

    ContactSystemController(
        ComputeCache& computeCache);
    ~ContactSystemController();

    void applyContactPackage(const ContactPackage& contactPackage);
    const ContactPackage& getContactPackage() const { return contactPackageStorage; }
    const ContactSystemRuntime& getRuntime() const { return runtime; }

    bool computePreviewForPayload(
        const NodePayloadRegistry& payloadRegistry,
        const ContactPairData& pair,
        ContactSystem::Result& outPreview,
        HandleInfo& outHandle,
        bool forceRebuild,
        bool& outPreviewUpdated);

    bool computePairsForPayload(
        const NodePayloadRegistry& payloadRegistry,
        const ContactPairData& pair,
        std::vector<ContactPair>& outPairs,
        bool forceRebuild = false);

    bool computePairs(
        const ContactPairPayloadConfig& pair,
        std::vector<ContactPair>& outPairs,
        bool forceRebuild = false);

    void clearCache();

private:
    static uint64_t buildCacheKey(const ContactPairData& pair);
    static uint64_t buildCacheKey(const ContactPairPayloadConfig& pair);
    static uint64_t buildDomainKey();
    static bool resolvePayloadContactPair(
        const NodePayloadRegistry& payloadRegistry,
        const ContactPairData& pair,
        ContactPairPayloadConfig& outPayloadPair);

    ContactSystem contactSystem;
    ComputeCache& computeCache;
    ContactPackage contactPackageStorage{};
    ContactSystemRuntime runtime;
};
