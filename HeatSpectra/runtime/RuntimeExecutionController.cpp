#include "RuntimeExecutionController.hpp"

#include "RuntimeInputController.hpp"
#include "RuntimeRenderController.hpp"
#include "RuntimeSimulationController.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "scene/CameraController.hpp"

RuntimeExecutionController::RuntimeExecutionController(
    RuntimeInputController& inputController,
    NodeGraphController& nodeGraphController,
    RuntimeRenderController& renderController,
    RuntimeSimulationController& simulationController,
    CameraController& cameraController,
    std::atomic<bool>& renderPaused)
    : inputController(inputController),
      nodeGraphController(nodeGraphController),
      renderController(renderController),
      simulationController(simulationController),
      cameraController(cameraController),
      renderPaused(renderPaused) {
}

void RuntimeExecutionController::tick(float deltaTime, uint32_t& frameCounter) {
    hasFrameSlot = false;

    inputController.tick(deltaTime);
    nodeGraphController.applyPendingChanges();
    if (renderPaused.load(std::memory_order_acquire)) {
        return;
    }

    nodeGraphController.tick();
    cameraController.tick(deltaTime);
    const bool allowHeatSolve = simulationController.canExecuteHeatSolve();
    const RuntimeRenderFrameResult renderResult = renderController.renderFrame(allowHeatSolve, frameCounter);
    hasFrameSlot = renderResult.submitted;
    frameSlot = renderResult.frameSlot;
}

bool RuntimeExecutionController::hasLastFrameSlot() const {
    return hasFrameSlot;
}

uint32_t RuntimeExecutionController::lastFrameSlot() const {
    return frameSlot;
}
