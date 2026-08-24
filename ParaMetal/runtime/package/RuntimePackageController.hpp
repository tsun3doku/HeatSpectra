#pragma once

#include "runtime/RuntimeConnections.hpp"
#include "runtime/package/RuntimePackageManager.hpp"

#include <memory>

class RuntimeProductManager;
class VulkanDevice;
class MemoryAllocator;
struct NodeGraphNode;

class RuntimePackageController {
public:
    RuntimePackageController(
        const RuntimeConnections& connections,
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator);

    void beginCompilation();
    bool applyNode(const NodeGraphNode& node);
    void finishCompilation();

    RuntimePackageManager& packages() { return packageManager; }
    const RuntimePackageManager& packages() const { return packageManager; }
    RuntimeProductManager* products() { return productManager.get(); }
    const RuntimeProductManager* products() const { return productManager.get(); }

private:
    bool applyPackage(uint64_t socketKey, const ModelPackage& package);
    bool applyPackage(uint64_t socketKey, const PointPackage& package);
    bool applyPackage(uint64_t socketKey, const RemeshPackage& package);
    bool applyPackage(uint64_t socketKey, const VoronoiPackage& package);
    bool applyPackage(uint64_t socketKey, const HeatPackage& package);
    void removeStalePackages();

    RuntimeConnections connections{};
    RuntimePackageManager packageManager{};
    std::unique_ptr<RuntimeProductManager> productManager;
};
