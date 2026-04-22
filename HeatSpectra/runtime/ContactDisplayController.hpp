#pragma once

#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace render {
class ContactOverlayRenderer;
}

class ContactDisplayController {
public:
    struct Config {
        bool showContactLines = false;
        bool authoredActive = false;
        bool hasValidContact = false;
        uint32_t emitterRuntimeModelId = 0;
        uint32_t receiverRuntimeModelId = 0;
        std::vector<ContactLineVertex> outlineVertices;
        std::vector<ContactLineVertex> correspondenceVertices;
        uint64_t displayHash = 0;

        bool anyVisible() const {
            return showContactLines;
        }

        bool isValid() const {
            return emitterRuntimeModelId != 0 &&
                receiverRuntimeModelId != 0 &&
                (!outlineVertices.empty() || !correspondenceVertices.empty());
        }

    };

    void setOverlayRenderer(render::ContactOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    render::ContactOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> configsBySocket;
    std::unordered_set<uint64_t> syncedSockets;
};

inline uint64_t buildDisplayHash(const ContactDisplayController::Config& config, uint64_t productHash) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showContactLines ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, productHash);
    return hash;
}