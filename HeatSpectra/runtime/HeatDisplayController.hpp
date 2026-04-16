#pragma once

#include "heat/HeatSystemDisplayController.hpp"
#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace render {
class HeatOverlayRenderer;
}

class HeatDisplayController {
public:
    struct Config {
        bool showHeatOverlay = false;
        bool authoredActive = false;
        bool active = false;
        bool paused = false;
        std::vector<ModelProduct> sourceModels;
        std::vector<float> sourceTemperatures;
        std::vector<ModelProduct> receiverModels;
        std::vector<std::array<VkBufferView, 11>> receiverBufferViews;
        uint64_t contentHash = 0;

        bool anyVisible() const {
            return showHeatOverlay;
        }

        bool isValid() const {
            return !receiverModels.empty() &&
                sourceModels.size() == sourceTemperatures.size() &&
                receiverModels.size() == receiverBufferViews.size();
        }

        bool operator==(const Config& other) const {
            return showHeatOverlay == other.showHeatOverlay &&
                authoredActive == other.authoredActive &&
                active == other.active &&
                paused == other.paused &&
                sourceModels == other.sourceModels &&
                sourceTemperatures == other.sourceTemperatures &&
                receiverModels == other.receiverModels &&
                receiverBufferViews == other.receiverBufferViews &&
                contentHash == other.contentHash;
        }
    };

    void setController(HeatSystemDisplayController* updatedController);
    void setOverlayRenderer(render::HeatOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    HeatSystemDisplayController* controller = nullptr;
    render::HeatOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> activeConfigsBySocket;
    std::unordered_set<uint64_t> touchedSocketKeys;
};

inline uint64_t computeContentHash(const HeatDisplayController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showHeatOverlay ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.authoredActive ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.active ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.paused ? 1u : 0u));
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.sourceModels.size()));
    for (const ModelProduct& sourceModel : config.sourceModels) {
        hash = RuntimeProductHash::mixPod(hash, sourceModel.runtimeModelId);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.vertexBuffer);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.vertexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.indexBuffer);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.indexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.indexCount);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.renderVertexBuffer);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.renderVertexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.renderIndexBuffer);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.renderIndexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.renderIndexCount);
        hash = RuntimeProductHash::mixPod(hash, sourceModel.modelMatrix);
    }
    hash = RuntimeProductHash::mixPodVector(hash, config.sourceTemperatures);
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverModels.size()));
    for (const ModelProduct& receiverModel : config.receiverModels) {
        hash = RuntimeProductHash::mixPod(hash, receiverModel.runtimeModelId);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.vertexBuffer);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.vertexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.indexBuffer);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.indexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.indexCount);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.renderVertexBuffer);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.renderVertexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.renderIndexBuffer);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.renderIndexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.renderIndexCount);
        hash = RuntimeProductHash::mixPod(hash, receiverModel.modelMatrix);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverBufferViews.size()));
    for (const auto& receiverBufferViews : config.receiverBufferViews) {
        for (VkBufferView bufferView : receiverBufferViews) {
            hash = RuntimeProductHash::mixPod(hash, bufferView);
        }
    }
    return hash;
}
