#include "FrameUpdateStage.hpp"

#include "scene/InputController.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/MaterialSystem.hpp"
#include "scene/ModelSelection.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "render/SceneRenderer.hpp"
#include "util/Structs.hpp"
#include "vulkan/UniformBufferManager.hpp"

FrameUpdateStage::FrameUpdateStage(
    InputController& inputController,
    UniformBufferManager& uniformBufferManager,
    ModelRegistry& modelRegistry,
    LightingSystem& lightingSystem,
    MaterialSystem& materialSystem,
    SceneRenderer& sceneRenderer,
    ModelSelection& modelSelection)
    : inputController(inputController),
      uniformBufferManager(uniformBufferManager),
      modelRegistry(modelRegistry),
      lightingSystem(lightingSystem),
      materialSystem(materialSystem),
      sceneRenderer(sceneRenderer),
      modelSelection(modelSelection) {
}

void FrameUpdateStage::processPicking(uint32_t frameIndex) {
    modelSelection.processPickingRequests(frameIndex);
}

void FrameUpdateStage::updateFrameState(uint32_t frameIndex, const render::SceneView& sceneView) {
    inputController.updateGizmo();

    UniformBufferObject ubo{};
    uniformBufferManager.updateUniformBuffer(frameIndex, sceneView, ubo);

    sceneRenderer.updateGrid(frameIndex, sceneView, modelRegistry.sceneWorldExtent());

    lightingSystem.update(frameIndex);
    materialSystem.update(frameIndex);
}

