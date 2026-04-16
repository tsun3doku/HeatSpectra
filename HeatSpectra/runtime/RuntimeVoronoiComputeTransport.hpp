#pragma once

#include "heat/VoronoiSystemComputeController.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

class RuntimeVoronoiComputeTransport {
public:
    void setController(VoronoiSystemComputeController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, VoronoiPackage>& packagesBySocket);
    void finalizeSync();

private:
    void removePublishedProduct(uint64_t socketKey);
    void publishProduct(uint64_t socketKey);

    VoronoiSystemComputeController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_set<uint64_t> publishedSocketKeys;
    std::vector<uint64_t> staleSocketKeys;
};
