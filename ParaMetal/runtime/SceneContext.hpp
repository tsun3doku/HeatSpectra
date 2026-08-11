#pragma once

#include <memory>

#include "scene/Camera.hpp"
#include "scene/CameraController.hpp"

class LightingSystem;
class IBLSystem;
class MaterialSystem;
class ModelUploader;
class ModelRegistry;
class UniformBufferManager;
class VulkanCoreContext;

class SceneContext {
public:
    SceneContext();
    ~SceneContext();

    bool initialize(VulkanCoreContext& core);
    void shutdown();
    bool isInitialized() const;

    CameraController& cameraController();
    const CameraController& cameraController() const;
    UniformBufferManager* uniformBufferManager();
    const UniformBufferManager* uniformBufferManager() const;
    ModelRegistry* modelRegistry();
    const ModelRegistry* modelRegistry() const;
    ModelUploader* modelUploader();
    const ModelUploader* modelUploader() const;
    MaterialSystem* materialSystem();
    const MaterialSystem* materialSystem() const;
    LightingSystem* lightingSystem();
    const LightingSystem* lightingSystem() const;
    IBLSystem* iblSystem();
    const IBLSystem* iblSystem() const;

private:
    Camera camera;
    CameraController cameraControllerState;
    std::unique_ptr<UniformBufferManager> uniformBufferManagerState;
    std::unique_ptr<ModelRegistry> modelRegistryState;
    std::unique_ptr<ModelUploader> modelUploaderState;
    std::unique_ptr<MaterialSystem> materialSystemState;
    std::unique_ptr<LightingSystem> lightingSystemState;
    std::unique_ptr<IBLSystem> iblSystemState;
    bool initialized = false;
};
