#pragma once

#include "contact/ContactSystemDisplayController.hpp"
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
        std::vector<ContactInterface::ContactLineVertex> outlineVertices;
        std::vector<ContactInterface::ContactLineVertex> correspondenceVertices;
        uint64_t contentHash = 0;

        bool anyVisible() const {
            return showContactLines;
        }

        bool isValid() const {
            return emitterRuntimeModelId != 0 &&
                receiverRuntimeModelId != 0 &&
                (!outlineVertices.empty() || !correspondenceVertices.empty());
        }

        bool operator==(const Config& other) const {
            return showContactLines == other.showContactLines &&
                authoredActive == other.authoredActive &&
                hasValidContact == other.hasValidContact &&
                emitterRuntimeModelId == other.emitterRuntimeModelId &&
                receiverRuntimeModelId == other.receiverRuntimeModelId &&
                outlineVertices == other.outlineVertices &&
                correspondenceVertices == other.correspondenceVertices &&
                contentHash == other.contentHash;
        }
    };

    void setController(ContactSystemDisplayController* updatedController);
    void setOverlayRenderer(render::ContactOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    ContactSystemDisplayController* controller = nullptr;
    render::ContactOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> activeConfigsBySocket;
    std::unordered_set<uint64_t> touchedSocketKeys;
};

inline uint64_t computeContentHash(const ContactDisplayController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showContactLines ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.authoredActive ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.hasValidContact ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, config.emitterRuntimeModelId);
    hash = RuntimeProductHash::mixPod(hash, config.receiverRuntimeModelId);
    hash = RuntimeProductHash::mixPodVector(hash, config.outlineVertices);
    hash = RuntimeProductHash::mixPodVector(hash, config.correspondenceVertices);
    return hash;
}
