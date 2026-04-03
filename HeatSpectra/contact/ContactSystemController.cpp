#include "ContactSystemController.hpp"

#include "runtime/ComputeCache.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <cstring>

ContactSystemController::ContactSystemController(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    ComputeCache& cache)
    : vulkanDevice(device),
      memoryAllocator(allocator),
      computeCache(cache) {
}

ContactSystemController::~ContactSystemController() {
    runtime.clear(memoryAllocator);
}

void ContactSystemController::configure(const Config& config) {
    runtime.rebuildProducts(
        this,
        vulkanDevice,
        memoryAllocator,
        config.binding);
}

void ContactSystemController::disable() {
    runtime.clear(memoryAllocator);
}

bool ContactSystemController::exportProduct(ContactProduct& outProduct) const {
    outProduct = {};
    const ContactProduct* product = runtime.getProduct();
    if (!product) {
        return false;
    }

    outProduct = *product;
    return outProduct.isValid();
}

bool ContactSystemController::computePreviewForRuntimePair(
    const RuntimeContactPairConfig& pair,
    ContactSystem::Result& outPreview,
    HandleInfo& outHandle,
    bool forceRebuild,
    bool& outPreviewUpdated) {
    if (pair.emitter.geometry.modelId == 0 ||
        pair.receiver.geometry.modelId == 0 ||
        pair.emitter.intrinsicMesh.vertices.empty() ||
        pair.receiver.intrinsicMesh.vertices.empty()) {
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
    if (!contactSystem.compute(pair, freshResult)) {
        return false;
    }

    const ComputeCache::Handle cacheHandle = computeCache.store(domainKey, inputHash, freshResult);
    outPreview = freshResult;
    outPreviewUpdated = true;
    outHandle.key = cacheHandle.key;
    outHandle.revision = cacheHandle.revision;
    outHandle.count = cacheHandle.count;
    outHandle.hasContact = outPreview.hasContact;
    return outHandle.hasContact;
}

bool ContactSystemController::computePairs(
    const RuntimeContactPairConfig& pair,
    std::vector<ContactPair>& outPairs,
    bool forceRebuild) {
    outPairs.clear();
    if (!pair.emitter.geometry.modelId ||
        !pair.receiver.geometry.modelId ||
        pair.emitter.intrinsicMesh.vertices.empty() ||
        pair.receiver.intrinsicMesh.vertices.empty()) {
        return false;
    }

    if (!forceRebuild) {
        const uint64_t domainKey = buildDomainKey();
        const uint64_t inputHash = buildCacheKey(pair);
        ContactSystem::Result cachedResult{};
        ComputeCache::Handle cacheHandle{};
        if (computeCache.tryGet(domainKey, inputHash, cachedResult, cacheHandle)) {
            if (!cachedResult.hasContact) {
                return false;
            }
            outPairs = cachedResult.pairs;
            return !outPairs.empty();
        }
    }

    ContactSystem::Result freshResult{};
    if (!contactSystem.compute(pair, freshResult)) {
        return false;
    }

    if (!freshResult.hasContact) {
        return false;
    }
    outPairs = freshResult.pairs;
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

uint64_t ContactSystemController::buildCacheKey(const RuntimeContactPairConfig& pair) {
    uint64_t h = 0xcbf29ce484222325ull;
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.couplingType));
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.emitter.geometry.modelId));
    h = ComputeCache::combine(h, static_cast<uint64_t>(pair.receiver.geometry.modelId));
    h = ComputeCache::combine(h, pair.emitter.geometry.payloadHash);
    h = ComputeCache::combine(h, pair.receiver.geometry.payloadHash);
    uint32_t minNormalBits = 0;
    uint32_t contactRadiusBits = 0;
    std::memcpy(&minNormalBits, &pair.minNormalDot, sizeof(float));
    std::memcpy(&contactRadiusBits, &pair.contactRadius, sizeof(float));
    h = ComputeCache::combine(h, static_cast<uint64_t>(minNormalBits));
    h = ComputeCache::combine(h, static_cast<uint64_t>(contactRadiusBits));
    return h;
}
