#include "RuntimeVoronoiComputeTransport.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "util/GeometryUtils.hpp"

ProductHandle RuntimeVoronoiComputeTransport::apply(uint64_t socketKey, const VoronoiPackage& package) {
    if (!controller || !products || socketKey == 0) {
        return {};
    }

    if (!package.authored.active) {
        remove(socketKey);
        return {};
    }

    const uint64_t computeHash = package.hashes.simulation;

    VoronoiSystemComputeController::Config config{};
    config.active = true;
    config.cellSize = package.authored.cellSize;
    config.voxelResolution = package.authored.voxelResolution;
    config.pointDomainCorners = package.pointDomainCorners;

    if (package.domainType == DomainType::Global) {
        if (package.pointPositions.empty() ||
            package.globalRemeshProducts.size() != package.globalRemeshLocalToWorld.size()) {
            remove(socketKey);
            return {};
        }
        config.isGlobalDomain = true;
        config.sdfPadding = package.authored.sdfPadding;
        config.pointPositions = package.pointPositions;
        config.globalRemeshRuntimeModelIds.reserve(package.globalRemeshProducts.size());
        config.globalRemeshPositions.reserve(package.globalRemeshProducts.size());
        config.globalRemeshTriangleIndices.reserve(package.globalRemeshProducts.size());
        config.globalRemeshSurfacePositions.reserve(package.globalRemeshProducts.size());
        config.globalRemeshSurfaceTriangleIndices.reserve(package.globalRemeshProducts.size());
        for (size_t i = 0; i < package.globalRemeshProducts.size(); ++i) {
            const RemeshProduct* remeshProduct = products->resolve<RemeshProduct>(package.globalRemeshProducts[i]);
            if (!remeshProduct || remeshProduct->runtimeModelId == 0) {
                remove(socketKey);
                return {};
            }
            const glm::mat4 transform = toMat4(package.globalRemeshLocalToWorld[i]);
            std::vector<glm::vec3> positions;
            positions.reserve(remeshProduct->geometryPositions.size());
            for (const glm::vec3& position : remeshProduct->geometryPositions) {
                positions.push_back(glm::vec3(transform * glm::vec4(position, 1.0f)));
            }
            std::vector<glm::vec3> surfacePositions;
            surfacePositions.reserve(remeshProduct->surfacePositions.size());
            for (const glm::vec3& position : remeshProduct->surfacePositions) {
                surfacePositions.push_back(glm::vec3(transform * glm::vec4(position, 1.0f)));
            }
            config.globalRemeshRuntimeModelIds.push_back(remeshProduct->runtimeModelId);
            config.globalRemeshPositions.push_back(std::move(positions));
            config.globalRemeshTriangleIndices.push_back(remeshProduct->geometryTriangleIndices);
            config.globalRemeshSurfacePositions.push_back(std::move(surfacePositions));
            config.globalRemeshSurfaceTriangleIndices.push_back(remeshProduct->surfaceTriangleIndices);
        }
    } else if (package.domainType == DomainType::Points) {
        if (package.pointPositions.empty()) {
            controller->remove(socketKey);
            return {};
        }
        config.isPointDomain = true;
        config.pointPositions = package.pointPositions;
    }

    config.computeHash = computeHash;

    controller->apply(socketKey, config);

    VoronoiProduct product{};
    if (!controller->buildProduct(socketKey, product)) {
        return {};
    }

    ProductHandle handle = products->publish<VoronoiProduct>(socketKey, product);
    return handle;
}

void RuntimeVoronoiComputeTransport::remove(uint64_t socketKey) {
    if (!controller || socketKey == 0) {
        return;
    }
    controller->remove(socketKey);
}
