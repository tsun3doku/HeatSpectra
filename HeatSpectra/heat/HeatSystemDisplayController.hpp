#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

class HeatSystem;
class HeatSystemComputeController;

class HeatSystemDisplayController {
public:
    struct Config {
        bool showHeatOverlay = false;

        bool anyVisible() const {
            return showHeatOverlay;
        }
    };

    void setComputeController(HeatSystemComputeController* updatedComputeController);
    void apply(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    std::vector<HeatSystem*> getActiveSystems() const;

private:
    HeatSystemComputeController* computeController = nullptr;
    std::unordered_map<uint64_t, Config> activeConfigsBySocket;
};
