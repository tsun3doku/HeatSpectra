#include "SceneContext.hpp"

#include "VulkanCoreContext.hpp"
#include "render/RenderConfig.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/MaterialSystem.hpp"
#include "scene/ModelUploader.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/UniformBufferManager.hpp"

SceneContext::SceneContext()
    : cameraControllerState(camera) {
}

SceneContext::~SceneContext() = default;

bool SceneContext::initialize(VulkanCoreContext& core) {
    if (initialized) {
        return true;
    }

    auto* allocator = core.allocator();
    auto* commandPool = core.commandPool();
    if (!allocator || !commandPool) {
        return false;
    }

    uniformBufferManagerState = std::make_unique<UniformBufferManager>(core.device(), *allocator, renderconfig::MaxFramesInFlight);
    materialSystemState = std::make_unique<MaterialSystem>(*uniformBufferManagerState);
    lightingSystemState = std::make_unique<LightingSystem>(cameraControllerState.getCamera(), *uniformBufferManagerState);
    modelUploaderState = std::make_unique<ModelUploader>(core.device(), *allocator, cameraControllerState.getCamera(), *commandPool);
    resourceManagerState = std::make_unique<ModelRegistry>(*allocator);

    modelUploaderState->uploadInitialModels(*resourceManagerState);
    initialized = true;
    return true;
}

void SceneContext::shutdown() {
    if (resourceManagerState) {
        resourceManagerState->cleanup();
    }
    if (uniformBufferManagerState) {
        uniformBufferManagerState->cleanup(renderconfig::MaxFramesInFlight);
    }

    lightingSystemState.reset();
    materialSystemState.reset();
    modelUploaderState.reset();
    resourceManagerState.reset();
    uniformBufferManagerState.reset();
    initialized = false;
}

bool SceneContext::isInitialized() const {
    return initialized;
}

CameraController& SceneContext::cameraController() {
    return cameraControllerState;
}

const CameraController& SceneContext::cameraController() const {
    return cameraControllerState;
}

UniformBufferManager* SceneContext::uniformBufferManager() {
    return uniformBufferManagerState.get();
}

const UniformBufferManager* SceneContext::uniformBufferManager() const {
    return uniformBufferManagerState.get();
}

ModelRegistry* SceneContext::resourceManager() {
    return resourceManagerState.get();
}

const ModelRegistry* SceneContext::resourceManager() const {
    return resourceManagerState.get();
}

ModelUploader* SceneContext::modelUploader() {
    return modelUploaderState.get();
}

const ModelUploader* SceneContext::modelUploader() const {
    return modelUploaderState.get();
}

MaterialSystem* SceneContext::materialSystem() {
    return materialSystemState.get();
}

const MaterialSystem* SceneContext::materialSystem() const {
    return materialSystemState.get();
}

LightingSystem* SceneContext::lightingSystem() {
    return lightingSystemState.get();
}

const LightingSystem* SceneContext::lightingSystem() const {
    return lightingSystemState.get();
}