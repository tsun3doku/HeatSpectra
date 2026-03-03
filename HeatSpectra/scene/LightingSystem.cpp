#include "LightingSystem.hpp"

#include "Camera.hpp"
#include "vulkan/UniformBufferManager.hpp"

#include <cstring>

LightingSystem::LightingSystem(Camera& camera, UniformBufferManager& uniformBufferManager)
    : camera(camera), uniformBufferManager(uniformBufferManager) {
}

void LightingSystem::update(uint32_t frameIndex) {
    LightUniformBufferObject lightUbo{};
    lightUbo.lightPos_Key = glm::normalize(lightDir);
    lightUbo.lightPos_Rim = glm::normalize(rimDir);
    lightUbo.lightAmbient = ambientColor;
    lightUbo.lightParams = glm::vec4(keyIntensity, rimIntensity, ambientIntensity, 0.0f);
    lightUbo.cameraPos = camera.getPosition();

    if (!enabled) {
        lightUbo.lightPos_Key = glm::vec3(0.0f);
        lightUbo.lightPos_Rim = glm::vec3(0.0f);
        lightUbo.lightAmbient = glm::vec3(0.0f);
        lightUbo.lightParams = glm::vec4(0.0f);
        lightUbo.cameraPos = glm::vec3(0.0f);
    }

    auto& mapped = uniformBufferManager.getLightBuffersMapped();
    if (frameIndex < mapped.size() && mapped[frameIndex]) {
        std::memcpy(mapped[frameIndex], &lightUbo, sizeof(lightUbo));
    }
}

void LightingSystem::setDirectionalLight(const glm::vec3& direction, const glm::vec3& color) {
    lightDir = direction;
    lightColor = color;
}

void LightingSystem::setAmbient(const glm::vec3& ambient) {
    ambientColor = ambient;
}

void LightingSystem::setRimLight(const glm::vec3& direction, const glm::vec3& color) {
    rimDir = direction;
    rimColor = color;
}

void LightingSystem::setKeyAndRim(const glm::vec3& keyDir, const glm::vec3& rimDir) {
    lightDir = keyDir;
    this->rimDir = rimDir;
}

void LightingSystem::setColors(const glm::vec3& keyColor, const glm::vec3& rimColor, const glm::vec3& ambient) {
    lightColor = keyColor;
    this->rimColor = rimColor;
    ambientColor = ambient;
}

void LightingSystem::setIntensity(float keyIntensity, float rimIntensity) {
    this->keyIntensity = keyIntensity;
    this->rimIntensity = rimIntensity;
}

void LightingSystem::setEnabled(bool enabled) {
    this->enabled = enabled;
}
