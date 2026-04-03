#include "ContactSystem.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace {

bool hasUsableContactPairs(const std::vector<ContactPair>& pairs) {
    for (const ContactPair& pair : pairs) {
        if (pair.contactArea > 0.0f) {
            return true;
        }
    }

    return false;
}
}

static bool computeContactPairs(
    ContactInterface& contactInterface,
    const GeometryData& emitterGeometry,
    const SupportingHalfedge::IntrinsicMesh& emitterIntrinsicMesh,
    const GeometryData& receiverGeometry,
    const SupportingHalfedge::IntrinsicMesh& receiverIntrinsicMesh,
    ContactCouplingType couplingType,
    float minNormalDot,
    float contactRadius,
    ContactSystem::Result& outResult) {
    (void)couplingType;
    outResult = {};
    if (emitterGeometry.modelId == 0 ||
        receiverGeometry.modelId == 0 ||
        emitterGeometry.modelId == receiverGeometry.modelId ||
        emitterIntrinsicMesh.vertices.empty() ||
        receiverIntrinsicMesh.vertices.empty()) {
        return false;
    }

    ContactInterface::Settings settings{};
    settings.minNormalDot = minNormalDot;
    settings.contactRadius = contactRadius;

    std::vector<std::vector<ContactPair>> receiverContactPairs;
    std::vector<const SupportingHalfedge::IntrinsicMesh*> receiverIntrinsicMeshes;
    std::vector<std::array<float, 16>> receiverLocalToWorld;
    receiverIntrinsicMeshes.push_back(&receiverIntrinsicMesh);
    receiverLocalToWorld.push_back(receiverGeometry.localToWorld);

    contactInterface.mapSurfacePoints(
        emitterIntrinsicMesh,
        emitterGeometry.localToWorld,
        receiverIntrinsicMeshes,
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

bool ContactSystem::compute(const RuntimeContactPairConfig& pair, Result& outResult) {
    return computeContactPairs(
        contactInterface,
        pair.emitter.geometry,
        pair.emitter.intrinsicMesh,
        pair.receiver.geometry,
        pair.receiver.intrinsicMesh,
        pair.couplingType,
        pair.minNormalDot,
        pair.contactRadius,
        outResult);
}
