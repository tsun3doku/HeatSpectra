#include "RemeshController.hpp"

#include "runtime/ModelRuntime.hpp"
#include "render/RenderRuntime.hpp"
#include "render/SceneRenderer.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>

namespace {

std::vector<glm::vec3> buildGeometryPositions(const std::vector<float>& pointPositions) {
    std::vector<glm::vec3> positions;
    positions.reserve(pointPositions.size() / 3);
    for (size_t index = 0; index + 2 < pointPositions.size(); index += 3) {
        positions.emplace_back(
            pointPositions[index],
            pointPositions[index + 1],
            pointPositions[index + 2]);
    }
    return positions;
}

} // namespace

RemeshController::RemeshController(
    Remesher& remesher,
    VulkanDevice& vulkanDevice,
    ModelRuntime& modelRuntime,
    ModelRegistry& resourceManager,
    RenderRuntime& renderRuntime,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      modelRuntime(modelRuntime),
      resourceManager(resourceManager),
      renderRuntime(renderRuntime),
      isOperating(isOperating),
      remesher(remesher) {
}

void RemeshController::configure(const Config& config) {
    if (config.socketKey == 0) {
        return;
    }

    flushRetiredStates(false);
    disableSocket(config.socketKey, false);
    flushRetiredStates(false);

    if (config.sourceGeometry.modelId == 0) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());
    OperatingScope operatingScope(isOperating);

    RemeshResult remeshResult{};
    if (!remesher.remesh(
            config.sourceGeometry.pointPositions,
            config.sourceGeometry.triangleIndices,
            config.params.iterations,
            config.params.minAngleDegrees,
            config.params.maxEdgeLength,
            config.params.stepSize,
            remeshResult)) {
        return;
    }

    if (remeshResult.intrinsicGpuResources.viewS == VK_NULL_HANDLE ||
        remeshResult.intrinsicGpuResources.intrinsicTriangleBuffer == VK_NULL_HANDLE ||
        remeshResult.intrinsicGpuResources.intrinsicVertexBuffer == VK_NULL_HANDLE) {
        cleanupGpuResources(remeshResult.intrinsicGpuResources);
        return;
    }

    GeometryData resolvedGeometry = config.sourceGeometry;
    resolvedGeometry.modelId = config.remeshHandle.key != 0
        ? static_cast<uint32_t>(config.remeshHandle.key & 0xFFFFFFFFu)
        : 0;

    ActiveState state{};
    state.config = config;
    state.geometry = resolvedGeometry;
    state.geometryPositions = buildGeometryPositions(config.sourceGeometry.pointPositions);
    state.geometryTriangleIndices = config.sourceGeometry.triangleIndices;
    state.intrinsicMesh = remeshResult.intrinsicMesh;
    state.intrinsicGpuResources = remeshResult.intrinsicGpuResources;
    state.sinkOwned = !resolvedGeometry.baseModelPath.empty();
    pendingStatesBySocket[config.socketKey] = std::move(state);
    modelRuntime.queueApplySink(
        resolvedGeometry.modelId,
        resolvedGeometry.baseModelPath,
        NodeModelTransform::toMat4(resolvedGeometry.localToWorld));
}

void RemeshController::disable(uint64_t socketKey) {
    disableSocket(socketKey, false);
    flushRetiredStates(false);
    modelRuntime.flush();
}

void RemeshController::disable() {
    std::vector<uint64_t> socketKeys;
    socketKeys.reserve(activeStatesBySocket.size());
    for (const auto& [socketKey, state] : activeStatesBySocket) {
        (void)state;
        socketKeys.push_back(socketKey);
    }

    for (uint64_t socketKey : socketKeys) {
        disableSocket(socketKey, false);
    }

    flushRetiredStates(false);
    modelRuntime.flush();

    modelRuntime.focusOnVisibleModel();
}

void RemeshController::finalizePendingStates() {
    std::vector<uint64_t> pendingSocketKeys;
    pendingSocketKeys.reserve(pendingStatesBySocket.size());
    for (const auto& [socketKey, state] : pendingStatesBySocket) {
        (void)state;
        pendingSocketKeys.push_back(socketKey);
    }

    for (uint64_t socketKey : pendingSocketKeys) {
        auto pendingIt = pendingStatesBySocket.find(socketKey);
        if (pendingIt == pendingStatesBySocket.end()) {
            continue;
        }

        ActiveState state = std::move(pendingIt->second);
        pendingStatesBySocket.erase(pendingIt);

        uint32_t runtimeModelId = 0;
        if (!modelRuntime.tryGetRuntimeModelId(state.geometry.modelId, runtimeModelId) || runtimeModelId == 0) {
            cleanupGpuResources(state.intrinsicGpuResources);
            continue;
        }

        state.runtimeModelId = runtimeModelId;
        activeStatesBySocket[socketKey] = state;

        RemeshProduct activeProduct{};
        if (exportProduct(socketKey, activeProduct)) {
            renderRuntime.getSceneRenderer().bindRemeshProduct(socketKey, activeProduct);
        } else {
            disableSocket(socketKey, false);
        }
    }
}

bool RemeshController::exportProduct(uint64_t socketKey, RemeshProduct& outProduct) const {
    outProduct = {};

    if (socketKey == 0) {
        return false;
    }

    const auto activeIt = activeStatesBySocket.find(socketKey);
    if (activeIt == activeStatesBySocket.end()) {
        return false;
    }

    const ActiveState& state = activeIt->second;
    if (state.config.socketKey == 0 ||
        state.runtimeModelId == 0 ||
        state.geometry.modelId == 0) {
        return false;
    }

    outProduct.runtimeModelId = state.runtimeModelId;
    outProduct.geometryPositions = state.geometryPositions;
    outProduct.geometryTriangleIndices = state.geometryTriangleIndices;
    outProduct.intrinsicMesh = state.intrinsicMesh;
    outProduct.intrinsicTriangleBuffer = state.intrinsicGpuResources.intrinsicTriangleBuffer;
    outProduct.intrinsicTriangleBufferOffset = state.intrinsicGpuResources.triangleGeometryOffset;
    outProduct.intrinsicVertexBuffer = state.intrinsicGpuResources.intrinsicVertexBuffer;
    outProduct.intrinsicVertexBufferOffset = state.intrinsicGpuResources.vertexGeometryOffset;
    outProduct.intrinsicTriangleCount = state.intrinsicGpuResources.triangleCount;
    outProduct.intrinsicVertexCount = state.intrinsicGpuResources.vertexCount;
    outProduct.averageTriangleArea = state.intrinsicGpuResources.averageTriangleArea;
    outProduct.supportingHalfedgeView = state.intrinsicGpuResources.viewS;
    outProduct.supportingAngleView = state.intrinsicGpuResources.viewA;
    outProduct.halfedgeView = state.intrinsicGpuResources.viewH;
    outProduct.edgeView = state.intrinsicGpuResources.viewE;
    outProduct.triangleView = state.intrinsicGpuResources.viewT;
    outProduct.lengthView = state.intrinsicGpuResources.viewL;
    outProduct.inputHalfedgeView = state.intrinsicGpuResources.viewHInput;
    outProduct.inputEdgeView = state.intrinsicGpuResources.viewEInput;
    outProduct.inputTriangleView = state.intrinsicGpuResources.viewTInput;
    outProduct.inputLengthView = state.intrinsicGpuResources.viewLInput;
    outProduct.contentHash = computeContentHash(outProduct);
    return outProduct.isValid();
}

void RemeshController::cleanupGpuResources(SupportingHalfedge::GPUResources& resources) const {
    auto destroyView = [this](VkBufferView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyBufferView(vulkanDevice.getDevice(), view, nullptr);
            view = VK_NULL_HANDLE;
        }
    };
    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            resourceManager.getMemoryAllocator().free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    destroyView(resources.viewS);
    destroyView(resources.viewA);
    destroyView(resources.viewH);
    destroyView(resources.viewE);
    destroyView(resources.viewT);
    destroyView(resources.viewL);
    destroyView(resources.viewHInput);
    destroyView(resources.viewEInput);
    destroyView(resources.viewTInput);
    destroyView(resources.viewLInput);

    freeBuffer(resources.bufferS, resources.offsetS);
    freeBuffer(resources.bufferA, resources.offsetA);
    freeBuffer(resources.bufferH, resources.offsetH);
    freeBuffer(resources.bufferE, resources.offsetE);
    freeBuffer(resources.bufferT, resources.offsetT);
    freeBuffer(resources.bufferL, resources.offsetL);
    freeBuffer(resources.bufferHInput, resources.offsetHInput);
    freeBuffer(resources.bufferEInput, resources.offsetEInput);
    freeBuffer(resources.bufferTInput, resources.offsetTInput);
    freeBuffer(resources.bufferLInput, resources.offsetLInput);
    freeBuffer(resources.intrinsicTriangleBuffer, resources.triangleGeometryOffset);
    freeBuffer(resources.intrinsicVertexBuffer, resources.vertexGeometryOffset);

    resources = {};
}

void RemeshController::disableSocket(uint64_t socketKey, bool updateFocus) {
    if (socketKey == 0) {
        return;
    }

    auto pendingIt = pendingStatesBySocket.find(socketKey);
    if (pendingIt != pendingStatesBySocket.end()) {
        ActiveState pendingState = std::move(pendingIt->second);
        pendingStatesBySocket.erase(pendingIt);
        cleanupGpuResources(pendingState.intrinsicGpuResources);
        if (pendingState.sinkOwned && pendingState.geometry.modelId != 0) {
            modelRuntime.queueRemoveSink(pendingState.geometry.modelId);
        }
    }

    auto activeIt = activeStatesBySocket.find(socketKey);
    if (activeIt == activeStatesBySocket.end()) {
        return;
    }

    ActiveState state = std::move(activeIt->second);

    renderRuntime.getSceneRenderer().removeIntrinsicPackage(socketKey);
    activeStatesBySocket.erase(activeIt);
    retiredStates.push_back(std::move(state));

    if (updateFocus) {
        modelRuntime.focusOnVisibleModel();
    }
}

void RemeshController::flushRetiredStates(bool updateFocus) {
    if (retiredStates.empty()) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());

    bool removedSink = false;
    for (ActiveState& state : retiredStates) {
        cleanupGpuResources(state.intrinsicGpuResources);

        if (state.sinkOwned && state.geometry.modelId != 0) {
            modelRuntime.queueRemoveSink(state.geometry.modelId);
            removedSink = true;
        }
    }
    retiredStates.clear();

    if (updateFocus && removedSink) {
        modelRuntime.queueFocusVisibleModel();
    }
}

RemeshController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

RemeshController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}

