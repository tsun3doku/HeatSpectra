#include "RemeshController.hpp"

#include "render/RenderRuntime.hpp"
#include "render/SceneRenderer.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "scene/Model.hpp"
#include "scene/SceneController.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
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
    ResourceManager& resourceManager,
    RenderRuntime& renderRuntime,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      resourceManager(resourceManager),
      renderRuntime(renderRuntime),
      isOperating(isOperating),
      remesher(remesher) {
}

void RemeshController::setSceneController(SceneController* updatedSceneController) {
    sceneController = updatedSceneController;
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
        std::cout << "[RemeshController] Remesh execution failed"
                  << " socketKey=" << config.socketKey
                  << " remeshHandle=" << config.remeshHandle.key << ":" << config.remeshHandle.revision
                  << std::endl;
        return;
    }

    if (remeshResult.intrinsicGpuResources.viewS == VK_NULL_HANDLE ||
        remeshResult.intrinsicGpuResources.intrinsicTriangleBuffer == VK_NULL_HANDLE ||
        remeshResult.intrinsicGpuResources.intrinsicVertexBuffer == VK_NULL_HANDLE) {
        std::cout << "[RemeshController] Failed to materialize intrinsic GPU resources"
                  << " socketKey=" << config.socketKey
                  << " remeshHandle=" << config.remeshHandle.key << ":" << config.remeshHandle.revision
                  << std::endl;
        cleanupGpuResources(remeshResult.intrinsicGpuResources);
        return;
    }

    GeometryData resolvedGeometry = config.sourceGeometry;
    resolvedGeometry.modelId = config.remeshHandle.key != 0
        ? static_cast<uint32_t>(config.remeshHandle.key & 0xFFFFFFFFu)
        : 0;

    const uint32_t runtimeModelId = materializeRuntimeModelSink(resolvedGeometry);
    std::cout << "[RemeshController] Remesh sink resolution"
              << " socketKey=" << config.socketKey
              << " geometryModelId=" << resolvedGeometry.modelId
              << " baseModelPath=" << resolvedGeometry.baseModelPath
              << " runtimeModelId=" << runtimeModelId
              << std::endl;
    if (runtimeModelId == 0) {
        cleanupGpuResources(remeshResult.intrinsicGpuResources);
        return;
    }

    Model* sinkModel = resourceManager.getModelByID(runtimeModelId);
    if (!sinkModel) {
        std::cout << "[RemeshController] Remesh sink model lookup failed"
                  << " socketKey=" << config.socketKey
                  << " runtimeModelId=" << runtimeModelId
                  << std::endl;
        cleanupGpuResources(remeshResult.intrinsicGpuResources);
        return;
    }

    sinkModel->setModelMatrix(NodeModelTransform::toMat4(resolvedGeometry.localToWorld));

    ActiveState state{};
    state.config = config;
    state.geometry = resolvedGeometry;
    state.geometryPositions = buildGeometryPositions(config.sourceGeometry.pointPositions);
    state.geometryTriangleIndices = config.sourceGeometry.triangleIndices;
    state.intrinsicMesh = remeshResult.intrinsicMesh;
    state.intrinsicGpuResources = remeshResult.intrinsicGpuResources;
    state.runtimeModelId = runtimeModelId;
    state.sinkOwned = !resolvedGeometry.baseModelPath.empty();
    activeStatesBySocket[config.socketKey] = state;

    RemeshProduct activeProduct{};
    if (exportProduct(config.socketKey, activeProduct)) {
        renderRuntime.getSceneRenderer().bindRemeshProduct(config.socketKey, activeProduct);
        std::cout << "[RemeshController] Bound remesh product to sink"
                  << " socketKey=" << config.socketKey
                  << " runtimeModelId=" << runtimeModelId
                  << std::endl;
    } else {
        disableSocket(config.socketKey, false);
    }
}

void RemeshController::disable(uint64_t socketKey) {
    disableSocket(socketKey, false);
    flushRetiredStates(false);
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

    if (sceneController) {
        sceneController->focusOnVisibleModel();
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
        state.config.remeshHandle.key == 0 ||
        state.runtimeModelId == 0 ||
        state.geometry.modelId == 0) {
        return false;
    }

    outProduct.remeshHandle = state.config.remeshHandle;
    outProduct.runtimeModelId = state.runtimeModelId;
    outProduct.geometry = state.geometry;
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
    return outProduct.isValid();
}

uint32_t RemeshController::materializeRuntimeModelSink(const GeometryData& geometry) const {
    if (!sceneController || geometry.modelId == 0) {
        return 0;
    }

    if (!geometry.baseModelPath.empty()) {
        return sceneController->materializeModelSink(geometry.modelId, geometry.baseModelPath);
    }

    uint32_t runtimeModelId = 0;
    if (!sceneController->tryGetNodeModelRuntimeId(geometry.modelId, runtimeModelId)) {
        return 0;
    }
    return runtimeModelId;
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

    auto activeIt = activeStatesBySocket.find(socketKey);
    if (activeIt == activeStatesBySocket.end()) {
        return;
    }

    ActiveState state = std::move(activeIt->second);

    renderRuntime.getSceneRenderer().removeIntrinsicPackage(socketKey);
    activeStatesBySocket.erase(activeIt);
    retiredStates.push_back(std::move(state));

    if (updateFocus && sceneController) {
        sceneController->focusOnVisibleModel();
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

        if (state.sinkOwned && sceneController && state.geometry.modelId != 0) {
            sceneController->removeNodeModelSink(state.geometry.modelId);
            removedSink = true;
        }
    }
    retiredStates.clear();

    if (updateFocus && sceneController && removedSink) {
        sceneController->focusOnVisibleModel();
    }
}

RemeshController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

RemeshController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}
