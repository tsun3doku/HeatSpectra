#pragma once

#include <cstdint>

#include "scene/SceneView.hpp"

class InputController;
class UniformBufferManager;
class ResourceManager;
class LightingSystem;
class MaterialSystem;
class SceneRenderer;
class ModelSelection;

class FrameUpdateStage {
public:
    FrameUpdateStage(
        InputController& inputController,
        UniformBufferManager& uniformBufferManager,
        ResourceManager& resourceManager,
        LightingSystem& lightingSystem,
        MaterialSystem& materialSystem,
        SceneRenderer& sceneRenderer,
        ModelSelection& modelSelection);

    void processPicking(uint32_t frameIndex);
    void updateFrameState(uint32_t frameIndex, const render::SceneView& sceneView);

private:
    InputController& inputController;
    UniformBufferManager& uniformBufferManager;
    ResourceManager& resourceManager;
    LightingSystem& lightingSystem;
    MaterialSystem& materialSystem;
    SceneRenderer& sceneRenderer;
    ModelSelection& modelSelection;
};
