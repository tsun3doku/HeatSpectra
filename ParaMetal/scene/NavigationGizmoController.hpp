#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

class CameraController;

struct NavigationGizmoRenderData {
    VkExtent2D extent{};
    glm::quat cameraOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec2 originPx{0.0f};
    float sizePx = 0.0f;
    uint8_t hoveredRegion = 0;
    uint8_t pressedRegion = 0;
};

class NavigationGizmoController {
public:
    explicit NavigationGizmoController(CameraController& cameraController);

    void setEnabled(bool value);
    void setViewport(VkExtent2D extent, float dpiScale);
    bool handlePointerMove(float x, float y);
    bool handlePointerPress(float x, float y);
    bool handlePointerRelease(float x, float y);
    bool hasPointerCapture() const { return pressedRegion != 0; }

    uint8_t hitTest(float x, float y) const;
    NavigationGizmoRenderData getRenderData() const;

    static uint8_t encodeRegion(const glm::ivec3& cameraSide);
    static glm::ivec3 decodeRegion(uint8_t region);

private:
    void snapToRegion(uint8_t region);

    CameraController& cameraController;
    VkExtent2D viewportExtent{};
    float dpiScale = 1.0f;
    glm::vec2 originPx{0.0f};
    float sizePx = 0.0f;
    uint8_t hoveredRegion = 0;
    uint8_t pressedRegion = 0;
    glm::vec2 pressPosition{0.0f};
    glm::vec2 lastPosition{0.0f};
    bool dragging = false;
    bool enabled = true;
};
