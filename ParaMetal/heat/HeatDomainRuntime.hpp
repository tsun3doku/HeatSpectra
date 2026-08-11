#pragma once

#include "heat/HeatContactRuntime.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

class HeatModelRuntime;

class HeatDomainRuntime {
public:
    ~HeatDomainRuntime();

    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& getActiveModels() const { return activeModels; }
    HeatModelRuntime* getModelByRuntimeId(uint32_t runtimeModelId) const;
    void removeModelsNotIn(const std::unordered_set<uint32_t>& runtimeModelIds);
    void setModel(uint32_t runtimeModelId, std::unique_ptr<HeatModelRuntime> model);
    void removeModel(uint32_t runtimeModelId);

    HeatContactRuntime& getContactRuntime() { return contactRuntime; }
    const HeatContactRuntime& getContactRuntime() const { return contactRuntime; }
    void cleanup();

private:
    std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>> activeModels;
    HeatContactRuntime contactRuntime;
};
