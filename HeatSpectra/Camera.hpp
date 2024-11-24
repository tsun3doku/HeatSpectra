#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL

class Camera {
public:
    void update(float deltaTime);  // Update position based on velocity
    void processKeyInput(GLFWwindow* window);  // Process key input
    void processMouseMovement(GLFWwindow* window);
    void processMouseScroll(double xOffset, double yOffset);// Process mouse movement
    bool isMousePressed;
    glm::mat4 getViewMatrix() const;  // Get the view matrix for the camera
    glm::mat4 getProjectionMatrix(float aspectRatio) const;  // Get the projection matrix for the camera

    glm::vec3 getPosition() const {
        return position;  // Return the camera's position
    }

    glm::vec3 getForwardDirection() const {
        // Calculate the forward direction based on pitch and yaw
        glm::vec3 forward;
        forward.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
        forward.y = sin(glm::radians(pitch));
        forward.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
        return forward;  // Normalize to ensure it's a unit vector
    }

    float radius = 2.0f; // Camera distance from origin

    float sensitivity = 0.3f;  // Mouse interaction speed

private:
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);  // Starting position
    glm::vec3 lookAt = glm::vec3(0.0f, 0.25f, 0.0f);     // Looking at the origin
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);         // Up vector

    glm::vec3 velocity = glm::vec3(0.0f);  // Movement velocity (WASD)

    void updateLookAt();  // Update lookAt based on pitch and yaw
    glm::mat4 getRotationMatrix() const;  // Rotation matrix based on pitch and yaw

    float pitch = 0.0f;
    float yaw = -90.0f;
    float roll = 0.0f;

    float nearPlane = 0.1f, farPlane = 100.0f;

    float fovVelocity = 0.0f;
    float dampingFactor = 0.1f;
    float currentFov = 45.0f;
    float maxVelocity = 100.0f;
};

#endif