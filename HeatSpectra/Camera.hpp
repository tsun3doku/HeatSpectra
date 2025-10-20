#pragma once 

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL

class Camera {
public:
    void update(float deltaTime);   
    void processKeyInput(bool wPressed, bool sPressed, bool aPressed, bool dPressed, bool qPressed, bool ePressed, bool shiftPressed, float deltaTime);  
    void processMouseMovement(bool middleButtonPressed, double mouseX, double mouseY);  
    void processMouseScroll(double xOffset, double yOffset);    
    void setLookAt(const glm::vec3& target);
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
        // Calculate the forward direction based on pitch and yaw
        glm::vec3 forward;
        forward.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
        forward.y = sin(glm::radians(pitch));
        forward.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
        return forward; 
    }

    float radius = 3.0f;        // Camera distance from origin
    float sensitivity = 0.3f;   // Mouse interaction speed

private:
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);   // Starting position    
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);         // Up vector     
    glm::vec3 lookAt;

    glm::vec3 velocity = glm::vec3(0.0f);               // Movement velocity (WASD)

    glm::mat4 getRotationMatrix() const;                // Rotation matrix based on pitch and yaw
    glm::mat4 getProjMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(currentFov), aspectRatio, nearPlane, farPlane);
    }

    float pitch = 0.0f;
    float yaw = -90.0f;
    float roll = 0.0f;
    float movementSpeed = 60.0f;

    float nearPlane = 0.1f, farPlane = 100.0f;

    float fovVelocity = 0.0f;
    float dampingFactor = 0.1f;
    float currentFov = 45.0f;
    float maxVelocity = 100.0f;
};