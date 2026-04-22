#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/HeatDisplayController.hpp"
#include "runtime/RuntimeECS.hpp"

#include <unordered_set>
#include <vector>

class RuntimeHeatDisplayTransport {
public:
    void setController(HeatDisplayController* updatedController) {
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

        auto view = registry.view<HeatPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            if (visibleKeys && visibleKeys->find(socketKey) == visibleKeys->end()) {
                continue;
            }

            const auto& package = registry.get<HeatPackage>(entity);
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
    void applyPackage(uint64_t socketKey, const HeatPackage& package) {
        if (!controller || socketKey == 0) {
            return;
        }

        if (!package.display.showHeatOverlay) {
            controller->remove(socketKey);
            return;
        }

        const HeatProduct* computeProduct = tryGetProduct<HeatProduct>(*ecsRegistry, socketKey);
        if (!computeProduct || !computeProduct->isValid()) {
            controller->remove(socketKey);
            return;
        }

        HeatDisplayController::Config config{};
        config.showHeatOverlay = package.display.showHeatOverlay;
        config.authoredActive = package.authored.active;
        config.active = computeProduct->active;
        config.paused = computeProduct->paused;
        std::unordered_map<uint32_t, VkBufferView> surfaceBufferViewByRuntimeModelId;
        surfaceBufferViewByRuntimeModelId.reserve(computeProduct->receiverRuntimeModelIds.size());
        for (size_t index = 0; index < computeProduct->receiverRuntimeModelIds.size(); ++index) {
            surfaceBufferViewByRuntimeModelId[computeProduct->receiverRuntimeModelIds[index]] =
                computeProduct->receiverSurfaceBufferViews[index];
        }

        for (size_t index = 0; index < package.sourceModelProducts.size(); ++index) {
            const ProductHandle& modelHandle = package.sourceModelProducts[index];
            const ProductHandle& remeshHandle = package.sourceRemeshProducts[index];
            const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, modelHandle.outputSocketKey);
            const RemeshProduct* remeshProduct = tryGetProduct<RemeshProduct>(*ecsRegistry, remeshHandle.outputSocketKey);
            if (!remeshProduct || !modelProduct || modelProduct->runtimeModelId == 0) {
                controller->remove(socketKey);
                return;
            }

            config.sourceModels.push_back(*modelProduct);
            config.sourceTemperatures.push_back(package.sourceTemperatures[index]);
        }

        for (size_t index = 0; index < package.receiverModelProducts.size(); ++index) {
            const ProductHandle& modelHandle = package.receiverModelProducts[index];
            const ProductHandle& remeshHandle = package.receiverRemeshProducts[index];
            const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, modelHandle.outputSocketKey);
            const RemeshProduct* remeshProduct = tryGetProduct<RemeshProduct>(*ecsRegistry, remeshHandle.outputSocketKey);
            if (!remeshProduct || !modelProduct || modelProduct->runtimeModelId == 0) {
                controller->remove(socketKey);
                return;
            }

            const auto surfaceBufferViewIt = surfaceBufferViewByRuntimeModelId.find(modelProduct->runtimeModelId);
            std::array<VkBufferView, 11> receiverBufferViews = {
                remeshProduct->supportingHalfedgeView,
                remeshProduct->supportingAngleView,
                remeshProduct->halfedgeView,
                remeshProduct->edgeView,
                remeshProduct->triangleView,
                remeshProduct->lengthView,
                remeshProduct->inputHalfedgeView,
                remeshProduct->inputEdgeView,
                remeshProduct->inputTriangleView,
                remeshProduct->inputLengthView,
                surfaceBufferViewIt != surfaceBufferViewByRuntimeModelId.end()
                    ? surfaceBufferViewIt->second
                    : VK_NULL_HANDLE
            };
            bool receiverValid = true;
            for (VkBufferView bufferView : receiverBufferViews) {
                if (bufferView == VK_NULL_HANDLE) {
                    receiverValid = false;
                    break;
                }
            }
            if (!receiverValid) {
                controller->remove(socketKey);
                return;
            }

            config.receiverModels.push_back(*modelProduct);
            config.receiverBufferViews.push_back(receiverBufferViews);
        }

        config.displayHash = buildDisplayHash(config, computeProduct->productHash);
        controller->apply(socketKey, config);
    }

    HeatDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
