#include "RuntimeModelComputeTransport.hpp"

#include "util/GeometryUtils.hpp"
#include "runtime/ModelComputeController.hpp"

ProductHandle RuntimeModelComputeTransport::apply(uint64_t socketKey, const ModelPackage& package) {
    if (!controller || !products || socketKey == 0) {
        return {};
    }

    ModelComputeController::Config config{};
    config.pointPositions = package.sourceGeometry.pointPositions;
    config.triangleIndices = package.sourceGeometry.triangleIndices;
    config.renderPositions = package.sourceGeometry.renderPositions;
    config.renderNormals = package.sourceGeometry.renderNormals;
    config.renderTexcoords = package.sourceGeometry.renderTexcoords;
    config.renderIndices = package.sourceGeometry.renderIndices;
    config.computeHash = package.hashes.geometry;
    controller->apply(socketKey, config);

    ModelProduct product{};
    if (!controller->buildProduct(socketKey, product)) {
        return {};
    }

    ProductHandle handle = products->publish<ModelProduct>(socketKey, product);
    return handle;
}


void RuntimeModelComputeTransport::remove(uint64_t socketKey) {
    if (!controller || socketKey == 0) {
        return;
    }
    controller->remove(socketKey);
}
