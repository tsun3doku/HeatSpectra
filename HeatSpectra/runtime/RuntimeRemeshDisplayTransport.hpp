#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/RemeshDisplayController.hpp"

#include <unordered_set>
#include <vector>

class RuntimeRemeshDisplayTransport {
public:
    void setController(RemeshDisplayController* updatedController) {
        controller = updatedController;
    }

    void setECSRegistry(ECSRegistry* updatedRegistry) {
        ecsRegistry = updatedRegistry;
    }

    void setVisibleKeys(const std::unordered_set<uint64_t>* keys) {
        visibleKeys = keys;
    }

    void sync(const ECSRegistry& registry) {
        if (!controller) {
            return;
        }

        auto view = registry.view<RemeshPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            if (visibleKeys && visibleKeys->find(socketKey) == visibleKeys->end()) {
                continue;
        }

        const auto& package = registry.get<RemeshPackage>(entity);
        RemeshDisplayController::Config config{};
        if (!tryBuildConfig(socketKey, package, config)) {
            controller->remove(socketKey);
            continue;
        }

        controller->apply(socketKey, config);
    }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    bool tryBuildConfig(uint64_t socketKey, const RemeshPackage& package, RemeshDisplayController::Config& outConfig) const {
        if (!controller || !ecsRegistry || socketKey == 0) {
            return false;
        }
        const bool anyVisible =
            package.display.showRemeshOverlay ||
            package.display.showFaceNormals ||
            package.display.showVertexNormals;
        if (!anyVisible) {
            return false;
        }

        const RemeshProduct* computeProduct = tryGetProduct<RemeshProduct>(*ecsRegistry, socketKey);
        if (!computeProduct || !computeProduct->isValid()) {
            return false;
        }
        if (package.modelProductHandle.outputSocketKey == 0) {
            return false;
        }

        const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, package.modelProductHandle.outputSocketKey);
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
        outConfig.modelMatrix = modelProduct->modelMatrix;
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
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->productHash);
        return true;
    }

    RemeshDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
