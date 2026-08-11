#include "HeatDomainRuntime.hpp"

#include "heat/HeatModelRuntime.hpp"

HeatDomainRuntime::~HeatDomainRuntime() = default;

HeatModelRuntime* HeatDomainRuntime::getModelByRuntimeId(uint32_t runtimeModelId) const {
    auto it = activeModels.find(runtimeModelId);
    return (it != activeModels.end()) ? it->second.get() : nullptr;
}

void HeatDomainRuntime::removeModelsNotIn(const std::unordered_set<uint32_t>& runtimeModelIds) {
    for (auto it = activeModels.begin(); it != activeModels.end();) {
        if (runtimeModelIds.find(it->first) == runtimeModelIds.end()) {
            it = activeModels.erase(it);
        } else {
            ++it;
        }
    }
}

void HeatDomainRuntime::setModel(
    uint32_t runtimeModelId,
    std::unique_ptr<HeatModelRuntime> model) {
    activeModels[runtimeModelId] = std::move(model);
}

void HeatDomainRuntime::removeModel(uint32_t runtimeModelId) {
    activeModels.erase(runtimeModelId);
}

void HeatDomainRuntime::cleanup() {
    contactRuntime.cleanup();
    activeModels.clear();
}
