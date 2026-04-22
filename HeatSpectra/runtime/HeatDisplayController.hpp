#pragma once

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
        uint64_t displayHash = 0;

        bool anyVisible() const {
            return showHeatOverlay;
        }

        bool isValid() const {
            return !receiverModels.empty() &&
                sourceModels.size() == sourceTemperatures.size() &&
                receiverModels.size() == receiverBufferViews.size();
        }

    };

    void setOverlayRenderer(render::HeatOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    render::HeatOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> configsBySocket;
    std::unordered_set<uint64_t> syncedSockets;
};

inline uint64_t buildDisplayHash(const HeatDisplayController::Config& config, uint64_t productHash) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showHeatOverlay ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.authoredActive ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.active ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.paused ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, productHash);
    return hash;
}