#include "RuntimePackageController.hpp"

#include "nodegraph/NodeGraphState.hpp"
#include "runtime/RuntimeHeatComputeTransport.hpp"
#include "runtime/RuntimeHeatDisplayTransport.hpp"
#include "runtime/RuntimeModelComputeTransport.hpp"
#include "runtime/RuntimeModelDisplayTransport.hpp"
#include "runtime/RuntimePointComputeTransport.hpp"
#include "runtime/RuntimePointDisplayTransport.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeVoronoiComputeTransport.hpp"
#include "runtime/RuntimeVoronoiDisplayTransport.hpp"

RuntimePackageController::RuntimePackageController(
    const RuntimeConnections& runtimeConnections,
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator)
    : connections(runtimeConnections),
      productManager(std::make_unique<RuntimeProductManager>(vulkanDevice, memoryAllocator)) {
    RuntimeProductManager* products = productManager.get();
    if (connections.modelComputeTransport) connections.modelComputeTransport->setProducts(products);
    if (connections.pointComputeTransport) connections.pointComputeTransport->setProducts(products);
    if (connections.remeshComputeTransport) connections.remeshComputeTransport->setProducts(products);
    if (connections.voronoiComputeTransport) connections.voronoiComputeTransport->setProducts(products);
    if (connections.heatComputeTransport) connections.heatComputeTransport->setProducts(products);
    if (connections.modelDisplayTransport) connections.modelDisplayTransport->setProducts(products);
    if (connections.pointDisplayTransport) connections.pointDisplayTransport->setProducts(products);
    if (connections.remeshDisplayTransport) connections.remeshDisplayTransport->setProducts(products);
    if (connections.voronoiDisplayTransport) connections.voronoiDisplayTransport->setProducts(products);
    if (connections.heatDisplayTransport) connections.heatDisplayTransport->setProducts(products);
}

void RuntimePackageController::beginCompilation() {
    packageManager.beginCompile();
}

bool RuntimePackageController::applyNode(const NodeGraphNode& node) {
    if (!productManager) return false;
    for (const NodeGraphSocket& output : node.outputs) {
        const uint64_t socketKey = NodeSocketKey(node.id, output.id).value;
        if (const ModelPackage* package = packageManager.find<ModelPackage>(socketKey)) {
            if (!applyPackage(socketKey, *package)) return false;
            continue;
        }
        if (const PointPackage* package = packageManager.find<PointPackage>(socketKey)) {
            if (!applyPackage(socketKey, *package)) return false;
            continue;
        }
        if (const RemeshPackage* package = packageManager.find<RemeshPackage>(socketKey)) {
            if (!applyPackage(socketKey, *package)) return false;
            continue;
        }
        if (const VoronoiPackage* package = packageManager.find<VoronoiPackage>(socketKey)) {
            if (!applyPackage(socketKey, *package)) return false;
            continue;
        }
        if (const HeatPackage* package = packageManager.find<HeatPackage>(socketKey)) {
            if (!applyPackage(socketKey, *package)) return false;
        }
    }
    return true;
}

bool RuntimePackageController::applyPackage(uint64_t socketKey, const ModelPackage& package) {
    if (!connections.modelComputeTransport) return true;
    const ProductHandle handle = connections.modelComputeTransport->apply(socketKey, package);
    if (!handle.isValid()) return false;
    packageManager.setProductHandle(socketKey, handle);
    return true;
}

bool RuntimePackageController::applyPackage(uint64_t socketKey, const PointPackage& package) {
    if (!connections.pointComputeTransport) return true;
    const ProductHandle handle = connections.pointComputeTransport->apply(socketKey, package);
    if (!handle.isValid()) return false;
    packageManager.setProductHandle(socketKey, handle);
    return true;
}

bool RuntimePackageController::applyPackage(uint64_t socketKey, const RemeshPackage& package) {
    if (!connections.remeshComputeTransport) return true;
    const ProductHandle handle = connections.remeshComputeTransport->apply(socketKey, package);
    if (!handle.isValid()) return false;
    packageManager.setProductHandle(socketKey, handle);
    return true;
}

bool RuntimePackageController::applyPackage(uint64_t socketKey, const VoronoiPackage& package) {
    if (!connections.voronoiComputeTransport) return true;
    const ProductHandle handle = connections.voronoiComputeTransport->apply(socketKey, package);
    if (!handle.isValid()) return false;
    packageManager.setProductHandle(socketKey, handle);
    return true;
}

bool RuntimePackageController::applyPackage(uint64_t socketKey, const HeatPackage& package) {
    if (!connections.heatComputeTransport) return true;
    const ProductHandle handle = connections.heatComputeTransport->apply(socketKey, package);
    if (!handle.isValid()) return false;
    packageManager.setProductHandle(socketKey, handle);
    return true;
}

void RuntimePackageController::finishCompilation() {
    removeStalePackages();
}

void RuntimePackageController::removeStalePackages() {
    for (uint64_t key : packageManager.staleSocketKeys()) {
        if (connections.modelComputeTransport && packageManager.findStored<ModelPackage>(key)) {
            connections.modelComputeTransport->remove(key);
        } else if (connections.pointComputeTransport && packageManager.findStored<PointPackage>(key)) {
            connections.pointComputeTransport->remove(key);
        } else if (connections.remeshComputeTransport && packageManager.findStored<RemeshPackage>(key)) {
            connections.remeshComputeTransport->remove(key);
        } else if (connections.voronoiComputeTransport && packageManager.findStored<VoronoiPackage>(key)) {
            connections.voronoiComputeTransport->remove(key);
        } else if (connections.heatComputeTransport && packageManager.findStored<HeatPackage>(key)) {
            connections.heatComputeTransport->remove(key);
        }
    }
    packageManager.destroyStale();
}
