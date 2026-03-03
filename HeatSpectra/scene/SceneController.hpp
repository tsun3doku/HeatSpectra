#pragma once

#include <atomic>
#include <cstdint>
#include <string>

class CameraController;
class HeatSystemController;
class MeshModifiers;
class RenderRuntime;
class ResourceManager;
class RenderTargetManager;
class VulkanDevice;

class SceneController {
public:
    SceneController(
        VulkanDevice& vulkanDevice,
        RenderTargetManager& renderTargetManager,
        ResourceManager& resourceManager,
        MeshModifiers& meshModifiers,
        RenderRuntime& renderRuntime,
        HeatSystemController& heatSystemController,
        CameraController& cameraController,
        std::atomic<bool>& isOperating);

    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t modelId);
    void performRemeshing(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize, uint32_t targetModelId = 0);
    void focusOnVisibleModel();

private:
    class OperatingScope {
    public:
        explicit OperatingScope(std::atomic<bool>& isOperating);
        ~OperatingScope();

    private:
        std::atomic<bool>& isOperating;
    };

    VulkanDevice& vulkanDevice;
    RenderTargetManager& renderTargetManager;
    ResourceManager& resourceManager;
    MeshModifiers& meshModifiers;
    RenderRuntime& renderRuntime;
    HeatSystemController& heatSystemController;
    CameraController& cameraController;
    std::atomic<bool>& isOperating;
};

