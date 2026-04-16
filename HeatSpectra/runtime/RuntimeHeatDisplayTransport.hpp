#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/HeatDisplayController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

class RuntimeHeatDisplayTransport {
public:
    void setController(HeatDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        computeProductRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, HeatPackage>& packagesBySocket) {
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
    void applyPackage(uint64_t socketKey, const HeatPackage& package) {
        if (!controller || !computeProductRegistry || socketKey == 0) {
            return;
        }

        if (!package.display.showHeatOverlay) {
            controller->remove(socketKey);
            return;
        }

        ProductHandle computeHandle =
            computeProductRegistry->getPublishedHandle(NodeProductType::Heat, socketKey);
        const HeatProduct* computeProduct =
            computeProductRegistry->resolveHeat(computeHandle);
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

        std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
        const size_t sourceCount = std::min(package.sourceModelProducts.size(), package.sourceRemeshProducts.size());
        for (size_t index = 0; index < sourceCount; ++index) {
            const ProductHandle& modelHandle = package.sourceModelProducts[index];
            const ProductHandle& remeshHandle = package.sourceRemeshProducts[index];
            const ModelProduct* modelProduct =
                computeProductRegistry->resolveModel(modelHandle);
            const RemeshProduct* remeshProduct =
                computeProductRegistry->resolveRemesh(remeshHandle);
            if (!remeshProduct || !modelProduct || modelProduct->runtimeModelId == 0) {
                controller->remove(socketKey);
                return;
            }

            const uint32_t runtimeModelId = modelProduct->runtimeModelId;
            if (!seenSourceRuntimeModelIds.insert(runtimeModelId).second) {
                continue;
            }

            config.sourceModels.push_back(*modelProduct);
            config.sourceTemperatures.push_back(
                (index < package.sourceTemperatures.size())
                ? package.sourceTemperatures[index]
                : 100.0f);
        }

        std::unordered_set<uint32_t> seenReceiverRuntimeModelIds;
        const size_t receiverCount = std::min(package.receiverModelProducts.size(), package.receiverRemeshProducts.size());
        for (size_t index = 0; index < receiverCount; ++index) {
            const ProductHandle& modelHandle = package.receiverModelProducts[index];
            const ProductHandle& remeshHandle = package.receiverRemeshProducts[index];
            const ModelProduct* modelProduct =
                computeProductRegistry->resolveModel(modelHandle);
            const RemeshProduct* remeshProduct =
                computeProductRegistry->resolveRemesh(remeshHandle);
            if (!remeshProduct || !modelProduct || modelProduct->runtimeModelId == 0) {
                controller->remove(socketKey);
                return;
            }

            const uint32_t runtimeModelId = modelProduct->runtimeModelId;
            if (!seenReceiverRuntimeModelIds.insert(runtimeModelId).second) {
                continue;
            }

            const auto surfaceBufferViewIt = surfaceBufferViewByRuntimeModelId.find(runtimeModelId);
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

        config.contentHash = computeContentHash(config);
        controller->apply(socketKey, config);
    }

    HeatDisplayController* controller = nullptr;
    RuntimeProductRegistry* computeProductRegistry = nullptr;
};
