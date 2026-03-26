#include "ContactSystem.hpp"

#include <vector>

bool ContactSystem::hasUsableContactPairs(const std::vector<ContactPair>& pairs) {
    for (const ContactPair& pair : pairs) {
        if (pair.contactArea > 0.0f) {
            return true;
        }
    }

    return false;
}

bool ContactSystem::compute(const ContactPairPayloadConfig& pair, Result& outResult) {
    outResult = {};
    if (pair.emitter.geometryHandle.key == 0 ||
        pair.receiver.geometryHandle.key == 0 ||
        pair.emitter.geometryHandle == pair.receiver.geometryHandle ||
        pair.emitter.intrinsic.vertices.empty() ||
        pair.receiver.intrinsic.vertices.empty()) {
        return false;
    }

    ContactInterface::Settings settings{};
    settings.minNormalDot = pair.minNormalDot;
    settings.contactRadius = pair.contactRadius;

    std::vector<std::vector<ContactPair>> receiverContactPairs;
    std::vector<const IntrinsicMeshData*> receiverIntrinsics;
    std::vector<std::array<float, 16>> receiverLocalToWorld;
    receiverIntrinsics.push_back(&pair.receiver.intrinsic);
    receiverLocalToWorld.push_back(pair.receiver.geometry.localToWorld);

    contactInterface.mapSurfacePoints(
        pair.emitter.intrinsic,
        pair.emitter.geometry.localToWorld,
        receiverIntrinsics,
        receiverLocalToWorld,
        receiverContactPairs,
        outResult.outlineVertices,
        outResult.correspondenceVertices,
        settings);

    if (!receiverContactPairs.empty()) {
        outResult.pairs = receiverContactPairs.front();
    }
    outResult.hasContact = hasUsableContactPairs(outResult.pairs);
    return outResult.hasContact;
}
