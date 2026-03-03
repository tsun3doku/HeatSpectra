#pragma once

#include <memory>

#include "scene/Camera.hpp"
#include "scene/CameraController.hpp"
#include "scene/ModelRegistry.hpp"

class LightingSystem;
class MaterialSystem;
class MeshModifiers;
class ModelUploader;
class ResourceManager;
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
    ResourceManager* resourceManager();
    const ResourceManager* resourceManager() const;
    MeshModifiers* meshModifiers();
    const MeshModifiers* meshModifiers() const;
    ModelUploader* modelUploader();
    const ModelUploader* modelUploader() const;
    MaterialSystem* materialSystem();
    const MaterialSystem* materialSystem() const;
    LightingSystem* lightingSystem();
    const LightingSystem* lightingSystem() const;
    ModelRegistry& modelRegistry();
    const ModelRegistry& modelRegistry() const;

private:
    Camera camera;
    CameraController cameraControllerState;
    std::unique_ptr<UniformBufferManager> uniformBufferManagerState;
    std::unique_ptr<ResourceManager> resourceManagerState;
    std::unique_ptr<MeshModifiers> meshModifiersState;
    std::unique_ptr<ModelUploader> modelUploaderState;
    std::unique_ptr<MaterialSystem> materialSystemState;
    std::unique_ptr<LightingSystem> lightingSystemState;
    ModelRegistry modelRegistryState;
    bool initialized = false;
};
