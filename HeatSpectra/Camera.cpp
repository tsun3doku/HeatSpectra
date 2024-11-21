#include "Camera.hpp"
#include <GLFW/glfw3.h>
#include <iostream>

void Camera::update(float deltaTime) {
    glm::mat4 rotationMatrix = getRotationMatrix();

    position.x = radius * cos(glm::radians(pitch)) * cos(glm::radians(yaw));
    position.y = radius * sin(glm::radians(pitch));
    position.z = radius * cos(glm::radians(pitch)) * sin(glm::radians(yaw));

    // Apply the roll to the up vector by rotating it around the front vector
    glm::mat4 rollMat = glm::rotate(glm::mat4(1.0f), glm::radians(roll), glm::normalize(position - lookAt));
    up = glm::normalize(glm::mat3(rollMat) * up); 
    roll = 0.0f; // Reset roll

    updateLookAt();

    fovVelocity *= (1.0f - dampingFactor); 

    currentFov += fovVelocity;  // Accumulate change in fov over time

    float minFov = 1.0f;
    float maxFov = 60.0f;
    if (currentFov < minFov) currentFov = minFov;
    if (currentFov > maxFov) currentFov = maxFov;
}

void Camera::processKeyInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        pitch += 1.0f;  // Pitch up
    }
    else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        pitch -= 1.0f;   // Pitch down
    }
 
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        yaw += 1.0f; // Rotate right around the model (clockwise around y-axis)
    }
    else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        yaw -= 1.0f; // Rotate left around the model (counter-clockwise around y-axis)
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        roll += 1.0f;  // Roll left (counter-clockwise around forward axis)
    }
    else if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        roll -= 1.0f;  // Roll right (clockwise around forward axis)
    }
    // Reset up vector when Shift+Q or Shift+E is pressed
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            // Reset up vector to default Y-up
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

void Camera::processMouseMovement(GLFWwindow* window) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
        static double lastX;
        static double lastY;
        
        if (!isMousePressed) {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);

            lastX = xpos;  // Set initial mouse position
            lastY = ypos;
            isMousePressed = true;  // Set true to track mouse movement
        }

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        double dx = xpos - lastX;  // Change in x
        double dy = ypos - lastY;  // Change in y

        yaw += dx * sensitivity;
        pitch += dy * sensitivity;

        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        // Update last positions
        lastX = xpos;
        lastY = ypos;
    }
    else {
        isMousePressed = false;  // Reset when mouse is released
    }
}

void Camera::processMouseScroll(double xOffset, double yOffset) {
    float zoomSpeed = 0.25f;  
    fovVelocity += (float)(-yOffset) * zoomSpeed;

    if (fovVelocity > maxVelocity) fovVelocity = maxVelocity;
    if (fovVelocity < -maxVelocity) fovVelocity = -maxVelocity;
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, lookAt, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(currentFov), aspectRatio, nearPlane, farPlane);
}

glm::mat4 Camera::getRotationMatrix() const {
    glm::mat4 rotationYaw = glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));  // Rotate around Y-axis (yaw)
    glm::mat4 rotationPitch = glm::rotate(glm::mat4(1.0f), glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));  // Rotate around X-axis (pitch)

    glm::mat4 rotationMatrix = rotationYaw * rotationPitch;
    return rotationMatrix;
}

void Camera::updateLookAt() {
    // Camera always looks at the origin
    lookAt = glm::vec3(0.0f, 0.0f, 0.0f);
}