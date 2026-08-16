#pragma once 

// Prevent Windows macros from interfering with GLM
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include "util/Units.hpp"

enum class CameraProjectionMode : uint8_t {
    Perspective = 0,
    Orthographic = 1
};

class Camera {
public:
    static constexpr float DefaultFov = 45.0f;
    static constexpr float DefaultZoomSpeed = 1.0f;
    static constexpr float MinZoomSpeed = 0.1f;
    static constexpr float MaxZoomSpeed = 5.0f;
    static constexpr float DefaultPanSpeed = 1.0f;
    static constexpr float MinPanSpeed = 0.1f;
    static constexpr float MaxPanSpeed = 5.0f;

    void update(float deltaTime);   
    void processMouseMovement(bool middleButtonPressed, double mouseX, double mouseY, bool shiftPressed = false);  
    void processMouseScroll(double yOffset);
    void setLookAt(const glm::vec3& target);
    void setOrientation(const glm::quat& q);
    void setRadius(float r);
    void setFov(float f);
    void setZoomSpeed(float speed);
    void setPanSpeed(float speed);
    void setProjectionMode(CameraProjectionMode mode);
    void setOrthographicHeight(float height);
    void orbit(float dx, float dy);
    void resetRadius();
    void setWorldUnit(units::LengthUnit unit);
    glm::vec3 screenToWorldRayOrigin(double mouseX, double mouseY, int screenWidth, int screenHeight) const;
    glm::vec3 screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight) const;
    void setState(
        const glm::vec3& target,
        const glm::quat& orientation,
        float radius,
        float fov,
        CameraProjectionMode projectionMode,
        float orthographicHeight,
        float zoomSpeed,
        float panSpeed);

    glm::mat4 getViewMatrix() const;  
    glm::mat4 getProjectionMatrix(float aspectRatio) const; 
    glm::vec3 getPosition() const {
        return position;  
    }

    float getFov() const {
        return currentFov;
    }

    float getBaseFov() const {
        return baseFov;
    }

    float getZoomSpeed() const {
        return zoomSpeed;
    }

    float getPanSpeed() const {
        return panSpeed;
    }

    glm::vec3 getLookAt() const {
        return lookAt;
    }

    glm::quat getOrientation() const {
        return orientation;
    }

    float getRadius() const {
        return radius;
    }

    CameraProjectionMode getProjectionMode() const {
        return projectionMode;
    }

    float getOrthographicHeight() const {
        return orthographicHeight;
    }

private:
    void pan(float dx, float dy);

    bool isMousePressed = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);   // Starting position    
    glm::vec3 lookAt;
    
    glm::quat orientation =
        glm::angleAxis(glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    float nearPlane = 0.01f, farPlane = 100.0f;

    float radiusVelocity = 0.0f;
    float radius = 2.0f;
    float sensitivity = 0.005f;
    static constexpr float panScale = 0.001f;
    float panSpeed = DefaultPanSpeed;
    float dampingFactor = 0.15f;
    static constexpr float zoomScale = 0.6f;
    static constexpr float zoomScaleOrtho = 0.72f;
    float zoomSpeed = DefaultZoomSpeed;
    float currentFov = DefaultFov;
    float baseFov = DefaultFov;
    float minFov = 10.0f; 
    float zoomThreshold = 2.0f;
    float maxRadiusVelocity = 300.0f;
    float maxOrthographicZoomVelocity = 7.2f;
    float minRadius = 0.1f;
    float maxRadius = 200.0f;

    CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;
    float orthographicHeight = 2.0f;
    float orthographicReferenceFov = DefaultFov;
    float orthographicZoomVelocity = 0.0f;
    float minOrthographicHeight = 0.001f;
    float maxOrthographicHeight = 1000.0f;
    units::LengthUnit worldUnit = units::defaultLengthUnit();
};
