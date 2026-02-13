#pragma once

#include <glm/glm.hpp>

#include "Structs.hpp"

class Camera;
class UniformBufferManager;

class LightingSystem {
public:
    LightingSystem(Camera& camera, UniformBufferManager& uniformBufferManager);

    void update(uint32_t frameIndex);

    void setDirectionalLight(const glm::vec3& direction, const glm::vec3& color);
    void setAmbient(const glm::vec3& ambient);
    void setRimLight(const glm::vec3& direction, const glm::vec3& color);
    void setKeyAndRim(const glm::vec3& keyDir, const glm::vec3& rimDir);
    void setColors(const glm::vec3& keyColor, const glm::vec3& rimColor, const glm::vec3& ambient);
    void setIntensity(float keyIntensity, float rimIntensity);
    void setEnabled(bool enabled);

private:
    Camera& camera;
    UniformBufferManager& uniformBufferManager;

    glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 rimDir = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 rimColor = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 ambientColor = glm::vec3(1.0f, 1.0f, 1.0f);
    float keyIntensity = 1.15f;
    float rimIntensity = 0.10f;
    float ambientIntensity = 0.12f;
    bool enabled = true;
};
