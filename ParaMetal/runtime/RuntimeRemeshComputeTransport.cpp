#include "RuntimeRemeshComputeTransport.hpp"
#include "runtime/RuntimeProducts.hpp"

ProductHandle RuntimeRemeshComputeTransport::apply(uint64_t socketKey, const RemeshPackage& package) {
    if (!controller || !products || socketKey == 0) {
        return {};
    }

    const ModelProduct* modelProduct = products->resolve<ModelProduct>(package.sourceModelProduct);
    if (!modelProduct || modelProduct->runtimeModelId == 0) {
        remove(socketKey);
        return {};
    }

    RemeshController::Config config{};
    config.pointPositions = package.sourceGeometry.pointPositions;
    config.triangleIndices = package.sourceGeometry.triangleIndices;
    config.iterations = package.iterations;
    config.minAngleDegrees = package.minAngleDegrees;
    config.maxEdgeLength = package.maxEdgeLength;
    config.stepSize = package.stepSize;
    config.runtimeModelId = modelProduct->runtimeModelId;
    config.computeHash = package.hashes.geometry;

    controller->apply(socketKey, config);

    RemeshProduct remeshProduct{};
    if (!controller->buildProduct(socketKey, remeshProduct)) {
        return {};
    }

    ProductHandle handle = products->publish<RemeshProduct>(socketKey, remeshProduct);
    return handle;
}


void RuntimeRemeshComputeTransport::remove(uint64_t socketKey) {
    if (!controller || socketKey == 0) {
        return;
    }
    controller->remove(socketKey);
}
