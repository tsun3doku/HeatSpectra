#pragma once

#include <atomic>
#include <cstdint>

class CameraController;
class NodeGraphController;
class RuntimeInputController;
class RuntimeRenderController;
class RuntimeSimulationController;

class RuntimeExecutionController {
public:
    RuntimeExecutionController(
        RuntimeInputController& inputController,
        NodeGraphController& nodeGraphController,
        RuntimeRenderController& renderController,
        RuntimeSimulationController& simulationController,
        CameraController& cameraController,
        std::atomic<bool>& renderPaused);

    void tick(float deltaTime, uint32_t& frameCounter);

    bool hasLastFrameSlot() const;
    uint32_t lastFrameSlot() const;

private:
    RuntimeInputController& inputController;
    NodeGraphController& nodeGraphController;
    RuntimeRenderController& renderController;
    RuntimeSimulationController& simulationController;
    CameraController& cameraController;
    std::atomic<bool>& renderPaused;

    bool hasFrameSlot = false;
    uint32_t frameSlot = 0;
};

