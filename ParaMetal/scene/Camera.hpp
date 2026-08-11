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
    void update(float deltaTime);   
    void processMouseMovement(bool middleButtonPressed, double mouseX, double mouseY, bool shiftPressed = false);  
    void processMouseScroll(double yOffset);
    void setLookAt(const glm::vec3& target);
    void setOrientation(const glm::quat& q);
    void setRadius(float r);
    void setFov(float f);
    void setProjectionMode(CameraProjectionMode mode);
    void setOrthographicHeight(float height);
    void pan(float dx, float dy);
    void orbit(float dx, float dy);
    void resetRadius();
    void setWorldUnit(units::LengthUnit unit);
    glm::vec3 screenToWorldRayOrigin(double mouseX, double mouseY, int screenWidth, int screenHeight) const;
    glm::vec3 screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight);

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

    float radius = 2.0f;            // Camera distance from origin
    float sensitivity = 0.005f;     // Mouse interaction speed 
    float panSensitivity = 0.001f;  // Panning speed multiplier

private:
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
    float dampingFactor = 0.15f;
    float currentFov = 45.0f;
    float baseFov = 45.0f;
    float minFov = 10.0f; 
    float zoomThreshold = 2.0f;
    float maxRadiusVelocity = 300.0f;
    float maxOrthographicZoomVelocity = 7.2f;
    float minRadius = 0.1f;
    float maxRadius = 200.0f;

    CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;
    float orthographicHeight = 2.0f;
    float orthographicReferenceFov = 45.0f;
    float orthographicZoomVelocity = 0.0f;
    float minOrthographicHeight = 0.001f;
    float maxOrthographicHeight = 1000.0f;
    units::LengthUnit worldUnit = units::defaultLengthUnit();
};
