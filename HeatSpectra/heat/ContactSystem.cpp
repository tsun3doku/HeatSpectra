#include "ContactSystem.hpp"

#include "scene/Model.hpp"
#include "vulkan/ResourceManager.hpp"

#include <utility>
#include <vector>

ContactSystem::ContactSystem(ResourceManager& resourceManagerRef, Remesher& remesher)
    : resourceManager(resourceManagerRef),
      contactInterface(remesher) {
}

ContactSystem::CachedResult* ContactSystem::findCachedResult(const ConfiguredContactPair& pair) {
    for (CachedResult& cachedResult : cachedResults) {
        if (cachedResult.pair == pair) {
            return &cachedResult;
        }
    }

    return nullptr;
}

bool ContactSystem::hasUsableContactPairs(const std::vector<ContactPairGPU>& pairs) {
    for (const ContactPairGPU& pair : pairs) {
        if (pair.contactArea > 0.0f) {
            return true;
        }
    }

    return false;
}

bool ContactSystem::compute(const ConfiguredContactPair& pair, Result& outResult, bool forceRebuild) {
    outResult = {};
    if (pair.emitterModelId == 0 ||
        pair.receiverModelId == 0 ||
        pair.emitterModelId == pair.receiverModelId) {
        return false;
    }

    if (!forceRebuild) {
        if (CachedResult* cachedResult = findCachedResult(pair)) {
            outResult = cachedResult->result;
            return outResult.hasContact;
        }
    }

    Model* emitterModel = resourceManager.getModelByID(pair.emitterModelId);
    Model* receiverModel = resourceManager.getModelByID(pair.receiverModelId);
    if (!emitterModel || !receiverModel) {
        return false;
    }

    ContactInterface::Settings settings{};
    settings.minNormalDot = pair.minNormalDot;
    settings.contactRadius = pair.contactRadius;

    std::vector<std::vector<ContactPairGPU>> receiverContactPairs;
    std::vector<Model*> receiverModels;
    receiverModels.push_back(receiverModel);

    contactInterface.mapSurfacePoints(
        *emitterModel,
        receiverModels,
        receiverContactPairs,
        outResult.outlineVertices,
        outResult.correspondenceVertices,
        settings);

    if (!receiverContactPairs.empty()) {
        outResult.pairs = receiverContactPairs.front();
    }
    outResult.hasContact = hasUsableContactPairs(outResult.pairs);

    if (CachedResult* cachedResult = findCachedResult(pair)) {
        cachedResult->result = outResult;
    } else {
        CachedResult newCachedResult{};
        newCachedResult.pair = pair;
        newCachedResult.result = outResult;
        cachedResults.push_back(std::move(newCachedResult));
    }

    return outResult.hasContact;
}

void ContactSystem::clearCache() {
    cachedResults.clear();
}
