#include "NodeGraphController.hpp"

#include "NodeGraphDebugCache.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodePayloadRegistry.hpp"
#include "runtime/RuntimeContactDisplayTransport.hpp"
#include "runtime/RuntimeHeatDisplayTransport.hpp"
#include "runtime/RuntimeModelDisplayTransport.hpp"
#include "runtime/RuntimePointDisplayTransport.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeVoronoiDisplayTransport.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>
#include <unordered_set>

NodeGraphController::NodeGraphController(
    NodePayloadRegistry& payloadRegistry,
    const RuntimeConnections& connections,
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator)
    : payloadRegistry(payloadRegistry),
      runtimeConnections(connections),
      runtime(&payloadRegistry),
      packageController(connections, vulkanDevice, memoryAllocator) {
    plan.isValid = false;
}

void NodeGraphController::rebuildForDelta(const NodeGraphDelta& delta) {
    runtime.applyDelta(delta);

    bool nonLayout = false;
    for (const NodeGraphChange& change : delta.changes) {
        if (change.reason != NodeGraphChangeReason::Layout) {
            nonLayout = true;
            break;
        }
    }
    if (!nonLayout) return;

    packageCompilationPending = true;
    NodeGraphDebugCache::instance().setState(runtime.state(), &payloadRegistry);
    plan = NodeGraphCompiler::compile(runtime.state());
}

void NodeGraphController::tick() {
    if (plan.isValid && packageCompilationPending) {
        if (compileRuntimePackages()) packageCompilationPending = false;
    }
    updateDisplayTransports();
}

void NodeGraphController::resetGraph(const NodeGraphState& state) {
    NodeGraphDelta delta{};
    delta.fromRevision = runtime.state().revision;
    delta.toRevision = state.revision;
    delta.changes.push_back(NodeGraphChange{});
    for (const auto& entry : state.nodes) {
        NodeGraphChange change{};
        change.type = NodeGraphChangeType::NodeUpsert;
        change.reason = NodeGraphChangeReason::Topology;
        change.node = entry.second;
        delta.changes.push_back(change);
    }
    for (const auto& entry : state.edges) {
        NodeGraphChange change{};
        change.type = NodeGraphChangeType::EdgeUpsert;
        change.reason = NodeGraphChangeReason::Topology;
        change.edge = entry.second;
        delta.changes.push_back(change);
    }
    rebuildForDelta(delta);
}

bool NodeGraphController::applyGraphDelta(const NodeGraphDelta& delta) {
    if (delta.fromRevision != runtime.state().revision) return false;
    rebuildForDelta(delta);
    return true;
}

const NodeGraphState& NodeGraphController::graphState() const {
    return runtime.state();
}

bool NodeGraphController::resolveGizmoTransformNode(
    uint64_t outputSocketKey,
    NodeGraphNodeId& outNodeId) const {
    outNodeId = {};
    return outputSocketKey != 0 &&
        findFirstUpstreamNodeByType(
            runtime.state(), outputSocketKey, nodegraphtypes::Transform, outNodeId);
}

bool NodeGraphController::compileRuntimePackages() {
    runtime.execute(plan);
    const NodeGraphEvaluation& evaluation = runtime.evaluation();
    const RuntimePackageCompiler::FrozenPackages frozenPackages =
        frozenPackageRebuildPending
            ? RuntimePackageCompiler::FrozenPackages::Rebuild
            : RuntimePackageCompiler::FrozenPackages::Preserve;
    std::vector<std::string> errors;
    if (!packageCompiler.validate(
            runtime.state(),
            plan,
            evaluation,
            payloadRegistry,
            errors)) {
        for (const std::string& error : errors) {
            std::cerr << "Runtime package compilation failed: " << error << '\n';
        }
        return false;
    }

    const RuntimeProductManager* products = packageController.products();
    if (!products) return false;
    packageController.beginCompilation();
    for (NodeGraphNodeId nodeId : plan.executionOrder) {
        const NodeGraphNode* node = runtime.state().node(nodeId);
        if (!node || !packageCompiler.compileNode(
                runtime.state(),
                *node,
                evaluation,
                payloadRegistry,
                *products,
                activeWorldUnit,
                frozenPackages,
                packageController.packages(),
                errors) ||
            !packageController.applyNode(*node)) {
            for (const std::string& error : errors) {
                std::cerr << "Runtime package compilation failed: " << error << '\n';
            }
            return false;
        }
    }
    packageController.finishCompilation();
    frozenPackageRebuildPending = false;

    NodeGraphDebugCache::instance().update(
        runtime.state().revision,
        evaluation.outputsBySocket);
    return true;
}

void NodeGraphController::setWorldUnit(units::LengthUnit unit) {
    if (unit == activeWorldUnit) return;
    activeWorldUnit = unit;
    packageCompilationPending = true;
    frozenPackageRebuildPending = true;
}

void NodeGraphController::updateDisplayTransports() {
    const bool hasDisplayTransports = runtimeConnections.modelDisplayTransport ||
        runtimeConnections.remeshDisplayTransport ||
        runtimeConnections.voronoiDisplayTransport ||
        runtimeConnections.contactDisplayTransport ||
        runtimeConnections.heatDisplayTransport ||
        runtimeConnections.pointDisplayTransport;
    if (!hasDisplayTransports) return;

    RuntimePackageManager& packages = packageController.packages();
    const std::unordered_set<uint64_t> visibleKeys =
        nodeGraphDisplay.computeDisplayKeys(
            runtime.state(),
            runtime.evaluation(),
            packages,
            &payloadRegistry);

    if (runtimeConnections.modelDisplayTransport) {
        runtimeConnections.modelDisplayTransport->sync(packages, visibleKeys);
        runtimeConnections.modelDisplayTransport->finalizeSync();
    }
    if (runtimeConnections.remeshDisplayTransport) {
        runtimeConnections.remeshDisplayTransport->sync(packages, visibleKeys);
        runtimeConnections.remeshDisplayTransport->finalizeSync();
    }
    if (runtimeConnections.voronoiDisplayTransport) {
        runtimeConnections.voronoiDisplayTransport->sync(packages, visibleKeys);
        runtimeConnections.voronoiDisplayTransport->finalizeSync();
    }
    if (runtimeConnections.contactDisplayTransport) {
        runtimeConnections.contactDisplayTransport->sync(packages, visibleKeys);
        runtimeConnections.contactDisplayTransport->finalizeSync();
    }
    if (runtimeConnections.heatDisplayTransport) {
        runtimeConnections.heatDisplayTransport->sync(packages, visibleKeys);
        runtimeConnections.heatDisplayTransport->finalizeSync();
    }
    if (runtimeConnections.pointDisplayTransport) {
        runtimeConnections.pointDisplayTransport->sync(packages, visibleKeys);
        runtimeConnections.pointDisplayTransport->finalizeSync();
    }
}

const NodeGraphCompiled& NodeGraphController::compiledState() const {
    return plan;
}

void NodeGraphController::addRuntimeModelId(
    std::vector<uint32_t>& outIds,
    uint32_t id) const {
    if (id == 0) return;
    for (uint32_t existing : outIds) {
        if (existing == id) return;
    }
    outIds.push_back(id);
}

bool NodeGraphController::runtimeModelIdsForSocket(
    uint64_t socketKey,
    std::vector<uint32_t>& outIds) const {
    const RuntimeProductManager* products = packageController.products();
    const RuntimePackageManager& packages = packageController.packages();
    if (socketKey == 0 || !products) return false;

    if (const ModelPackage* package = packages.findAny<ModelPackage>(socketKey)) {
        if (const ModelProduct* product = products->resolve<ModelProduct>(package->productHandle))
            addRuntimeModelId(outIds, product->runtimeModelId);
        return true;
    }
    if (const RemeshPackage* package = packages.findAny<RemeshPackage>(socketKey)) {
        if (const RemeshProduct* product = products->resolve<RemeshProduct>(package->productHandle))
            addRuntimeModelId(outIds, product->runtimeModelId);
        return true;
    }
    if (const VoronoiPackage* package = packages.findAny<VoronoiPackage>(socketKey)) {
        if (const VoronoiProduct* product = products->resolve<VoronoiProduct>(package->productHandle))
            addRuntimeModelId(outIds, product->runtimeModelId);
        return true;
    }
    if (const HeatPackage* package = packages.findAny<HeatPackage>(socketKey)) {
        for (const HeatModelPackage& model : package->models) {
            if (const ModelProduct* product = products->resolve<ModelProduct>(model.modelProduct))
                addRuntimeModelId(outIds, product->runtimeModelId);
        }
        return true;
    }
    if (const ContactPackage* package = packages.findAny<ContactPackage>(socketKey)) {
        if (const ContactProduct* product = products->resolve<ContactProduct>(package->productHandle)) {
            addRuntimeModelId(outIds, product->modelARuntimeModelId);
            addRuntimeModelId(outIds, product->modelBRuntimeModelId);
        }
        return true;
    }
    return packages.findAny<PointPackage>(socketKey) != nullptr;
}

bool NodeGraphController::runtimeModelIdsForNode(
    NodeGraphNodeId nodeId,
    std::vector<uint32_t>& outIds) const {
    outIds.clear();
    const auto nodeIt = runtime.state().nodes.find(nodeId.value);
    if (nodeIt == runtime.state().nodes.end()) return false;

    for (const NodeGraphSocket& output : nodeIt->second.outputs) {
        const uint64_t socketKey = NodeSocketKey(nodeIt->second.id, output.id).value;
        if (runtimeModelIdsForSocket(socketKey, outIds)) return true;
    }
    return false;
}
