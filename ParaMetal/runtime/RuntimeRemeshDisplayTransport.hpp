#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/package/RuntimePackageManager.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/RemeshDisplayController.hpp"
#include "util/GeometryUtils.hpp"

#include <unordered_set>
#include <vector>

class RuntimeRemeshDisplayTransport {
public:
    void setController(RemeshDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    void sync(const RuntimePackageManager& registry, const std::unordered_set<uint64_t>& visibleKeys) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> nextSocketKeys;
        for (const auto& [socketKey, package] : registry.remeshes()) {
            if (!registry.isCurrent(socketKey)) continue;
            if (visibleKeys.find(socketKey) == visibleKeys.end()) {
                continue;
            }

            RemeshDisplayController::Config config{};
            if (!tryBuildConfig(socketKey, package, config)) {
                controller->remove(socketKey);
                continue;
            }

            controller->apply(socketKey, config);
            nextSocketKeys.insert(socketKey);
        }

        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                controller->remove(socketKey);
            }
        }
        activeSocketKeys = nextSocketKeys;
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    bool tryBuildConfig(
        uint64_t socketKey,
        const RemeshPackage& package,
        RemeshDisplayController::Config& outConfig) const {
        if (!controller || !products || socketKey == 0) {
            return false;
        }
        const bool anyVisible =
            package.display.showRemeshOverlay ||
            package.display.showFaceNormals ||
            package.display.showVertexNormals;
        if (!anyVisible) {
            return false;
        }

        const RemeshProduct* computeProduct = products->resolve<RemeshProduct>(package.productHandle);
        if (!computeProduct || !computeProduct->isValid()) {
            return false;
        }

        const ModelProduct* modelProduct = products->resolve<ModelProduct>(package.sourceModelProduct);
        if (!modelProduct || modelProduct->runtimeModelId == 0) {
            return false;
        }

        outConfig = {};
        outConfig.showRemeshOverlay = package.display.showRemeshOverlay;
        outConfig.showFaceNormals = package.display.showFaceNormals;
        outConfig.showVertexNormals = package.display.showVertexNormals;
        outConfig.normalLength = package.display.normalLength;
        outConfig.runtimeModelId = modelProduct->runtimeModelId;
        outConfig.renderVertexBuffer = modelProduct->renderVertexBuffer;
        outConfig.renderVertexBufferOffset = modelProduct->renderVertexBufferOffset;
        outConfig.renderIndexBuffer = modelProduct->renderIndexBuffer;
        outConfig.renderIndexBufferOffset = modelProduct->renderIndexBufferOffset;
        outConfig.renderIndexCount = modelProduct->renderIndexCount;
        outConfig.modelMatrix = toMat4(package.localToWorld);
        outConfig.intrinsicTriangleBuffer = computeProduct->intrinsicTriangleBuffer;
        outConfig.intrinsicTriangleBufferOffset = computeProduct->intrinsicTriangleBufferOffset;
        outConfig.intrinsicVertexBuffer = computeProduct->intrinsicVertexBuffer;
        outConfig.intrinsicVertexBufferOffset = computeProduct->intrinsicVertexBufferOffset;
        outConfig.intrinsicTriangleCount = computeProduct->intrinsicTriangleCount;
        outConfig.intrinsicVertexCount = computeProduct->intrinsicVertexCount;
        outConfig.averageTriangleArea = computeProduct->averageTriangleArea;
        outConfig.supportingHalfedgeView = computeProduct->supportingHalfedgeView;
        outConfig.supportingAngleView = computeProduct->supportingAngleView;
        outConfig.halfedgeView = computeProduct->halfedgeView;
        outConfig.edgeView = computeProduct->edgeView;
        outConfig.triangleView = computeProduct->triangleView;
        outConfig.lengthView = computeProduct->lengthView;
        outConfig.inputHalfedgeView = computeProduct->inputHalfedgeView;
        outConfig.inputEdgeView = computeProduct->inputEdgeView;
        outConfig.inputTriangleView = computeProduct->inputTriangleView;
        outConfig.inputLengthView = computeProduct->inputLengthView;
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->hashes.display);
        return true;
    }

    RemeshDisplayController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
