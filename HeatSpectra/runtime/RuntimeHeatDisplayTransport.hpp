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
        HeatDisplayController::Config config{};
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
    bool tryBuildConfig(uint64_t socketKey, const HeatPackage& package, HeatDisplayController::Config& outConfig) const {
        if (!controller || !ecsRegistry || socketKey == 0) {
            return false;
        }
        if (!package.display.showHeatOverlay) {
            return false;
        }
        if (package.sourceModelProducts.size() != package.sourceTemperatures.size()) {
            return false;
        }
        if (package.receiverModelProducts.size() != package.receiverRemeshProducts.size()) {
            return false;
        }

        const HeatProduct* computeProduct = tryGetProduct<HeatProduct>(*ecsRegistry, socketKey);
        if (!computeProduct || !computeProduct->isValid()) {
            return false;
        }

        outConfig = {};
        outConfig.showHeatOverlay = package.display.showHeatOverlay;
        outConfig.authoredActive = package.authored.active;
        outConfig.active = computeProduct->active;
        outConfig.paused = computeProduct->paused;
        std::unordered_map<uint32_t, VkBufferView> surfaceBufferViewByRuntimeModelId;
        surfaceBufferViewByRuntimeModelId.reserve(computeProduct->receiverRuntimeModelIds.size());
        for (size_t index = 0; index < computeProduct->receiverRuntimeModelIds.size(); ++index) {
            surfaceBufferViewByRuntimeModelId[computeProduct->receiverRuntimeModelIds[index]] =
                computeProduct->receiverSurfaceBufferViews[index];
        }

        for (size_t index = 0; index < package.sourceModelProducts.size(); ++index) {
            const ProductHandle& modelHandle = package.sourceModelProducts[index];
            const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, modelHandle.outputSocketKey);
            if (!modelProduct || modelProduct->runtimeModelId == 0) {
                return false;
            }

            outConfig.sourceModels.push_back(*modelProduct);
            outConfig.sourceTemperatures.push_back(package.sourceTemperatures[index]);
        }

        for (size_t index = 0; index < package.receiverModelProducts.size(); ++index) {
            const ProductHandle& modelHandle = package.receiverModelProducts[index];
            const ProductHandle& remeshHandle = package.receiverRemeshProducts[index];
            const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, modelHandle.outputSocketKey);
            const RemeshProduct* remeshProduct = tryGetProduct<RemeshProduct>(*ecsRegistry, remeshHandle.outputSocketKey);
            if (!remeshProduct || !modelProduct || modelProduct->runtimeModelId == 0) {
                return false;
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
                return false;
            }

            outConfig.receiverModels.push_back(*modelProduct);
            outConfig.receiverBufferViews.push_back(receiverBufferViews);
        }

        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->productHash);
        return true;
    }

    HeatDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
