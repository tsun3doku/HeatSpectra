#include "RuntimePointComputeTransport.hpp"

#include "runtime/PointComputeRuntime.hpp"
#include "util/GeometryUtils.hpp"
#include <glm/gtc/matrix_transform.hpp>

ProductHandle RuntimePointComputeTransport::apply(uint64_t socketKey, const PointPackage& package) {
    if (!runtime || !products || socketKey == 0) {
        return {};
    }

    if (package.pointCount == 0 || package.positions.empty()) {
        runtime->disable(socketKey);
        return {};
    }

    const uint64_t inputHash = package.hashes.geometry;

    PointComputeRuntime::Config config{};
    config.socketKey = socketKey;
    config.positions = package.positions;
    config.modelMatrix = toMat4(package.localToWorld);
    config.computeHash = inputHash;

    PointProduct product{};
    if (!runtime->buildProduct(config, product)) {
        return {};
    }

    ProductHandle handle = products->publish<PointProduct>(socketKey, product);
    return handle;
}


void RuntimePointComputeTransport::remove(uint64_t socketKey) {
    if (!runtime || socketKey == 0) {
        return;
    }
    runtime->disable(socketKey);
}
