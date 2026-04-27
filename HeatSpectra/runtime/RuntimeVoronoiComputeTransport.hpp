#pragma once

#include "heat/VoronoiSystemComputeController.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

#include <unordered_set>
#include <vector>

class RuntimeVoronoiComputeTransport {
public:
    void setController(VoronoiSystemComputeController* updatedController) {
        controller = updatedController;
    }

    void setECSRegistry(ECSRegistry* updatedRegistry) {
        ecsRegistry = updatedRegistry;
    }

    void sync(const ECSRegistry& registry);
    void finalizeSync();

private:
    bool tryBuildConfig(uint64_t socketKey, const VoronoiPackage& package, VoronoiSystemComputeController::Config& outConfig) const;
    void removePublishedProduct(uint64_t socketKey);
    void publishProduct(uint64_t socketKey);

    VoronoiSystemComputeController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_map<uint64_t, uint64_t> appliedPackageHash;
};
