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

class Camera {
public:
    void update(float deltaTime);   
    void processMouseMovement(bool middleButtonPressed, double mouseX, double mouseY, bool shiftPressed = false);  
    void processMouseScroll(double xOffset, double yOffset);    
    void setLookAt(const glm::vec3& target);
    void pan(float dx, float dy);
    void resetRadius();
    glm::vec3 screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight);

    bool isMousePressed;
    glm::mat4 getViewMatrix() const;  
    glm::mat4 getProjectionMatrix(float aspectRatio) const; 
    glm::vec3 getPosition() const {
        return position;  
    }

    float getFov() const {
        return currentFov;
    }

    glm::vec3 getForwardDirection() const {
        return orientation * glm::vec3(0.0f, 0.0f, 1.0f);
    }

    float radius = 2.0f;            // Camera distance from origin
    float sensitivity = 0.005f;     // Mouse interaction speed 
    float panSensitivity = 0.001f;  // Panning speed multiplier

private:
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);   // Starting position    
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);         // Up vector     
    glm::vec3 lookAt;
    
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion

    glm::vec3 velocity = glm::vec3(0.0f);               

    glm::mat4 getRotationMatrix() const;                
    glm::mat4 getProjMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(currentFov), aspectRatio, nearPlane, farPlane);
    }

    float movementSpeed = 60.0f;

    float nearPlane = 0.01f, farPlane = 100.0f;

    float radiusVelocity = 0.0f;
    float dampingFactor = 0.1f;
    float currentFov = 45.0f;
    float baseFov = 45.0f;
    float minFov = 10.0f; 
    float zoomThreshold = 2.0f;
    float maxVelocity = 5.0f;
    float minRadius = 0.1f;
    float maxRadius = 200.0f;
};
