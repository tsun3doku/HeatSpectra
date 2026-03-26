#include "FrameUpdateStage.hpp"

#include "scene/InputController.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/MaterialSystem.hpp"
#include "scene/ModelSelection.hpp"
#include "vulkan/ResourceManager.hpp"
#include "render/SceneRenderer.hpp"
#include "util/Structs.hpp"
#include "vulkan/UniformBufferManager.hpp"

FrameUpdateStage::FrameUpdateStage(
    InputController& inputController,
    UniformBufferManager& uniformBufferManager,
    ResourceManager& resourceManager,
    LightingSystem& lightingSystem,
    MaterialSystem& materialSystem,
    SceneRenderer& sceneRenderer,
    ModelSelection& modelSelection)
    : inputController(inputController),
      uniformBufferManager(uniformBufferManager),
      resourceManager(resourceManager),
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

    GridUniformBufferObject gridUbo{};
    const glm::vec3 gridSize = resourceManager.calculateMaxBoundingBoxSize();
    uniformBufferManager.updateGridUniformBuffer(frameIndex, sceneView, gridUbo, gridSize);
    sceneRenderer.updateGridLabels(gridSize);

    lightingSystem.update(frameIndex);
    materialSystem.update(frameIndex);
}
