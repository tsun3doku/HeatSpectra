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
            applyPackage(socketKey, package);
        }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    void applyPackage(uint64_t socketKey, const RemeshPackage& package) {
        if (!controller || socketKey == 0) {
            return;
        }

        const bool anyVisible =
            package.display.showRemeshOverlay ||
            package.display.showFaceNormals ||
            package.display.showVertexNormals;
        if (!anyVisible) {
            controller->remove(socketKey);
            return;
        }

        const RemeshProduct* computeProduct = tryGetProduct<RemeshProduct>(*ecsRegistry, socketKey);
        if (!computeProduct || !computeProduct->isValid()) {
            controller->remove(socketKey);
            return;
        }
        if (package.modelProductHandle.outputSocketKey == 0) {
            controller->remove(socketKey);
            return;
        }

        const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, package.modelProductHandle.outputSocketKey);
        if (!modelProduct || modelProduct->runtimeModelId == 0) {
            controller->remove(socketKey);
            return;
        }

        RemeshDisplayController::Config config{};
        config.showRemeshOverlay = package.display.showRemeshOverlay;
        config.showFaceNormals = package.display.showFaceNormals;
        config.showVertexNormals = package.display.showVertexNormals;
        config.normalLength = package.display.normalLength;
        config.runtimeModelId = modelProduct->runtimeModelId;
        config.renderVertexBuffer = modelProduct->renderVertexBuffer;
        config.renderVertexBufferOffset = modelProduct->renderVertexBufferOffset;
        config.renderIndexBuffer = modelProduct->renderIndexBuffer;
        config.renderIndexBufferOffset = modelProduct->renderIndexBufferOffset;
        config.renderIndexCount = modelProduct->renderIndexCount;
        config.modelMatrix = modelProduct->modelMatrix;
        config.intrinsicTriangleBuffer = computeProduct->intrinsicTriangleBuffer;
        config.intrinsicTriangleBufferOffset = computeProduct->intrinsicTriangleBufferOffset;
        config.intrinsicVertexBuffer = computeProduct->intrinsicVertexBuffer;
        config.intrinsicVertexBufferOffset = computeProduct->intrinsicVertexBufferOffset;
        config.intrinsicTriangleCount = computeProduct->intrinsicTriangleCount;
        config.intrinsicVertexCount = computeProduct->intrinsicVertexCount;
        config.averageTriangleArea = computeProduct->averageTriangleArea;
        config.supportingHalfedgeView = computeProduct->supportingHalfedgeView;
        config.supportingAngleView = computeProduct->supportingAngleView;
        config.halfedgeView = computeProduct->halfedgeView;
        config.edgeView = computeProduct->edgeView;
        config.triangleView = computeProduct->triangleView;
        config.lengthView = computeProduct->lengthView;
        config.inputHalfedgeView = computeProduct->inputHalfedgeView;
        config.inputEdgeView = computeProduct->inputEdgeView;
        config.inputTriangleView = computeProduct->inputTriangleView;
        config.inputLengthView = computeProduct->inputLengthView;
        config.displayHash = buildDisplayHash(config, computeProduct->productHash);
        controller->apply(socketKey, config);
    }

    RemeshDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
