#pragma once

#include <cstdint>
#include <unordered_set>

class ContactSystemComputeController;

class ContactSystemDisplayController {
public:
    struct Config {
        bool showPreview = false;
    };

    void setComputeController(ContactSystemComputeController* updatedComputeController);
    void apply(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();

private:
    ContactSystemComputeController* computeController = nullptr;
    std::unordered_set<uint64_t> previewEnabledSockets;
};
