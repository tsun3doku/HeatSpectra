#include "RuntimeContactComputeTransport.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "util/GeometryUtils.hpp"

ProductHandle RuntimeContactComputeTransport::apply(uint64_t socketKey, const ContactPackage& package) {
    if (!controller || !products || socketKey == 0) {
        return {};
    }

    if (!package.authored.active || !package.authored.pair.hasValidContact) {
        controller->remove(socketKey);
        return {};
    }

    const RemeshProduct* modelARemeshProduct = products->resolve<RemeshProduct>(package.modelARemeshProduct);
    const RemeshProduct* modelBRemeshProduct = products->resolve<RemeshProduct>(package.modelBRemeshProduct);
    if (!modelARemeshProduct || !modelBRemeshProduct) {
        controller->remove(socketKey);
        return {};
    }

    const uint64_t computeHash = package.hashes.simulation;
    ContactSystemComputeController::Config config{};
    config.minNormalDot = package.authored.pair.minNormalDot;
    config.contactRadius = package.authored.pair.contactRadius;
    config.modelALocalToWorld = package.modelALocalToWorld;
    config.modelAMesh = buildContactMesh(
        modelARemeshProduct->surfacePositions,
        modelARemeshProduct->surfaceNormals,
        modelARemeshProduct->surfaceTriangleIndices);
    config.modelBLocalToWorld = package.modelBLocalToWorld;
    config.modelBMesh = buildContactMesh(
        modelBRemeshProduct->surfacePositions,
        modelBRemeshProduct->surfaceNormals,
        modelBRemeshProduct->surfaceTriangleIndices);
    config.modelARuntimeModelId = modelARemeshProduct->runtimeModelId;
    config.modelBRuntimeModelId = modelBRemeshProduct->runtimeModelId;
    config.computeHash = computeHash;
    if (!config.isValid()) {
        controller->remove(socketKey);
        return {};
    }

    controller->apply(socketKey, config);

    ContactProduct product{};
    if (!controller->buildProduct(socketKey, product)) {
        return {};
    }

    ProductHandle handle = products->publish<ContactProduct>(socketKey, product);
    return handle;
}


void RuntimeContactComputeTransport::remove(uint64_t socketKey) {
    if (!controller || socketKey == 0) {
        return;
    }
    controller->remove(socketKey);
}
