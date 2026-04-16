#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RemeshDisplayController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"

#include <unordered_map>

class RuntimeRemeshDisplayTransport {
public:
    void setController(RemeshDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        computeProductRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, RemeshPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        for (const auto& [socketKey, package] : packagesBySocket) {
            applyPackage(socketKey, package);
        }
    }

    void finalizeSync() {
        if (controller) {
            controller->finalizeSync();
        }
    }

private:
    void applyPackage(uint64_t socketKey, const RemeshPackage& package) {
        if (!controller || !computeProductRegistry || socketKey == 0) {
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

        ProductHandle remeshHandle =
            computeProductRegistry->getPublishedHandle(NodeProductType::Remesh, socketKey);
        const RemeshProduct* computeProduct =
            computeProductRegistry->resolveRemesh(remeshHandle);
        if (!computeProduct || !computeProduct->isValid()) {
            controller->remove(socketKey);
            return;
        }

        const ModelProduct* modelProduct =
            computeProductRegistry->resolveModel(package.modelProductHandle);
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
        config.contentHash = computeContentHash(config);
        controller->apply(socketKey, config);
    }

    RemeshDisplayController* controller = nullptr;
    RuntimeProductRegistry* computeProductRegistry = nullptr;
};
