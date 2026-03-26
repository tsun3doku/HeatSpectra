#include "ContactSystemController.hpp"

#include "runtime/ComputeCache.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

#include <cstring>

bool ContactSystemController::resolvePayloadContactPair(
    const NodePayloadRegistry& payloadRegistry,
    const ContactPairData& pair,
    ContactPairPayloadConfig& outPayloadPair) {
    outPayloadPair = {};
    if (!pair.hasValidContact ||
        pair.endpointA.geometryHandle.key == 0 ||
        pair.endpointB.geometryHandle.key == 0 ||
        pair.endpointA.geometryHandle == pair.endpointB.geometryHandle) {
        return false;
    }

    const ContactPairEndpoint& emitterEndpoint =
        (pair.endpointA.role == ContactPairRole::Source)
        ? pair.endpointA
        : pair.endpointB;
    const ContactPairEndpoint& receiverEndpoint =
        (pair.endpointA.role == ContactPairRole::Source)
        ? pair.endpointB
        : pair.endpointA;

    const GeometryData* emitterGeometry = payloadRegistry.get<GeometryData>(emitterEndpoint.geometryHandle);
    const GeometryData* receiverGeometry = payloadRegistry.get<GeometryData>(receiverEndpoint.geometryHandle);
    if (!emitterGeometry || !receiverGeometry ||
        emitterGeometry->intrinsicHandle.key == 0 ||
        receiverGeometry->intrinsicHandle.key == 0) {
        return false;
    }

    const IntrinsicMeshData* emitterIntrinsic = payloadRegistry.get<IntrinsicMeshData>(emitterGeometry->intrinsicHandle);
    const IntrinsicMeshData* receiverIntrinsic = payloadRegistry.get<IntrinsicMeshData>(receiverGeometry->intrinsicHandle);
    if (!emitterIntrinsic || !receiverIntrinsic) {
        return false;
    }

    outPayloadPair.couplingType = pair.kind;
    outPayloadPair.minNormalDot = pair.minNormalDot;
    outPayloadPair.contactRadius = pair.contactRadius;
    outPayloadPair.emitter.geometryHandle = emitterEndpoint.geometryHandle;
    outPayloadPair.emitter.geometry = *emitterGeometry;
    outPayloadPair.emitter.intrinsic = *emitterIntrinsic;
    outPayloadPair.receiver.geometryHandle = receiverEndpoint.geometryHandle;
    outPayloadPair.receiver.geometry = *receiverGeometry;
    outPayloadPair.receiver.intrinsic = *receiverIntrinsic;
    return true;
}

ContactSystemController::ContactSystemController(ComputeCache& cache)
    : computeCache(cache) {
}

ContactSystemController::~ContactSystemController() = default;

void ContactSystemController::applyContactPackage(const ContactPackage& contactPackage) {
    contactPackageStorage = contactPackage;

    std::vector<RuntimeContactBinding> configuredContacts;
    configuredContacts.reserve(contactPackageStorage.runtimeContactPairs.size());
    for (const RuntimeContactBinding& contactPair : contactPackageStorage.runtimeContactPairs) {
        configuredContacts.push_back(contactPair);
    }

    runtime.rebuildResolvedContacts(this, configuredContacts);
}

bool ContactSystemController::computePreviewForPayload(const NodePayloadRegistry& payloadRegistry,
    const ContactPairData& pair,
    ContactSystem::Result& outPreview, HandleInfo& outHandle, bool forceRebuild, bool& outPreviewUpdated) {
    ContactPairPayloadConfig payloadPair{};
    if (!resolvePayloadContactPair(payloadRegistry, pair, payloadPair)) {
        outPreview = {};
        outHandle = {};
        outPreviewUpdated = false;
        return false;
    }

    outPreview = {};
    outHandle = {};
    outPreviewUpdated = false;

    const uint64_t domainKey = buildDomainKey();
    const uint64_t inputHash = buildCacheKey(pair);
    if (!forceRebuild) {
        ContactSystem::Result cachedResult{};
        ComputeCache::Handle cacheHandle{};
        if (computeCache.tryGet(domainKey, inputHash, cachedResult, cacheHandle)) {
            outPreview = cachedResult;
            outHandle.key = cacheHandle.key;
            outHandle.revision = cacheHandle.revision;
            outHandle.count = cacheHandle.count;
            outHandle.hasContact = cachedResult.hasContact;
            outPreviewUpdated = cachedResult.hasContact;
            return cachedResult.hasContact;
        }
    }

    ContactSystem::Result freshResult{};
    if (!contactSystem.compute(payloadPair, freshResult)) {
        return false;
    }

    const ComputeCache::Handle cacheHandle = computeCache.store(domainKey, inputHash, freshResult);
    outPreview = std::move(freshResult);
    outPreviewUpdated = true;
    outHandle.key = cacheHandle.key;
    outHandle.revision = cacheHandle.revision;
    outHandle.count = cacheHandle.count;
    outHandle.hasContact = outPreview.hasContact;
    return outHandle.hasContact;
}

bool ContactSystemController::computePairsForPayload(
    const NodePayloadRegistry& payloadRegistry,
    const ContactPairData& pair,
    std::vector<ContactPair>& outPairs,
    bool forceRebuild) {
    outPairs.clear();

    ContactPairPayloadConfig payloadPair{};
    if (!resolvePayloadContactPair(payloadRegistry, pair, payloadPair)) {
        return false;
    }

    return computePairs(payloadPair, outPairs, forceRebuild);
}

bool ContactSystemController::computePairs(
    const ContactPairPayloadConfig& pair,
    std::vector<ContactPair>& outPairs,
    bool forceRebuild) {
    outPairs.clear();

    const uint64_t domainKey = buildDomainKey();
    const uint64_t inputHash = buildCacheKey(pair);
    if (!forceRebuild) {
        ContactSystem::Result cachedResult{};
        ComputeCache::Handle cacheHandle{};
        if (computeCache.tryGet(domainKey, inputHash, cachedResult, cacheHandle)) {
            if (!cachedResult.hasContact) {
                return false;
            }
            outPairs = std::move(cachedResult.pairs);
            return !outPairs.empty();
        }
    }

    ContactSystem::Result freshResult{};
    if (!contactSystem.compute(pair, freshResult)) {
        return false;
    }

    const ComputeCache::Handle cacheHandle = computeCache.store(domainKey, inputHash, freshResult);
    (void)cacheHandle;
    if (!freshResult.hasContact) {
        return false;
    }
    outPairs = std::move(freshResult.pairs);
    return !outPairs.empty();
}

void ContactSystemController::clearCache() {
    computeCache.clearDomain(buildDomainKey());
}

uint64_t ContactSystemController::buildDomainKey() {
    uint64_t h = 0xcbf29ce484222325ull;
    const char domain[] = "ContactPair";
    for (char c : domain) {
        h = ComputeCache::combine(h, static_cast<uint64_t>(static_cast<unsigned char>(c)));
    }
    return h;
}

uint64_t ContactSystemController::buildCacheKey(const ContactPairData& pair) {
    uint64_t h = 0xcbf29ce484222325ull;
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.kind));
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.endpointA.role));
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.endpointB.role));
    h = ComputeCache::combine(h, pair.endpointA.geometryHandle.key);
    h = ComputeCache::combine(h, pair.endpointA.geometryHandle.revision);
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.endpointA.geometryHandle.count));
    h = ComputeCache::combine(h, pair.endpointB.geometryHandle.key);
    h = ComputeCache::combine(h, pair.endpointB.geometryHandle.revision);
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.endpointB.geometryHandle.count));
    uint32_t minNormalBits = 0;
    uint32_t contactRadiusBits = 0;
    std::memcpy(&minNormalBits, &pair.minNormalDot, sizeof(float));
    std::memcpy(&contactRadiusBits, &pair.contactRadius, sizeof(float));
    h = ComputeCache::combine(h, static_cast<uint64_t>(minNormalBits));
    h = ComputeCache::combine(h, static_cast<uint64_t>(contactRadiusBits));
    return h;
}

uint64_t ContactSystemController::buildCacheKey(const ContactPairPayloadConfig& pair) {
    uint64_t h = 0xcbf29ce484222325ull;
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.couplingType));
    h = ComputeCache::combine(h, pair.emitter.geometryHandle.key);
    h = ComputeCache::combine(h, pair.emitter.geometryHandle.revision);
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.emitter.geometryHandle.count));
    h = ComputeCache::combine(h, pair.receiver.geometryHandle.key);
    h = ComputeCache::combine(h, pair.receiver.geometryHandle.revision);
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.receiver.geometryHandle.count));
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.emitter.geometry.modelId));
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.receiver.geometry.modelId));
    uint32_t minNormalBits = 0;
    uint32_t contactRadiusBits = 0;
    std::memcpy(&minNormalBits, &pair.minNormalDot, sizeof(float));
    std::memcpy(&contactRadiusBits, &pair.contactRadius, sizeof(float));
    h = ComputeCache::combine(h, static_cast<uint64_t>(minNormalBits));
    h = ComputeCache::combine(h, static_cast<uint64_t>(contactRadiusBits));
    return h;
}
