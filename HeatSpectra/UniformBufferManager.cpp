#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanDevice.hpp"
#include "Camera.hpp"
#include "UniformBufferManager.hpp"

void UniformBufferManager::init(VulkanDevice& vulkanDevice, VkExtent2D swapChainExtent) {
    this->vulkanDevice = &vulkanDevice; // Reference to VulkanDevice class
    this->swapChainExtent = swapChainExtent;

    std::cout << "Logical device in UniformBufferManager: " << vulkanDevice.getDevice() << std::endl;
    
    createUniformBuffers();
    createGridUniformBuffers();
    createLightUniformBuffers();
    createSSAOKernelBuffers();
}

void UniformBufferManager::createUniformBuffers() {
   
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i] = vulkanDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uniformBuffersMemory[i]);
        std::cout << "Created Uniform Buffer: " << uniformBuffers[i] << std::endl;
        std::cout << "Memory for Uniform Buffer: " << uniformBuffersMemory[i] << std::endl;

        vkMapMemory(vulkanDevice->getDevice(), uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
}

void UniformBufferManager::createGridUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(GridUniformBufferObject);

    gridUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    gridUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    gridUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        gridUniformBuffers[i] = vulkanDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
            gridUniformBuffersMemory[i]);
        std::cout << "Created Grid Uniform Buffer: " << gridUniformBuffers[i] << std::endl;
        std::cout << "Memory for Grid Uniform Buffer: " << gridUniformBuffersMemory[i] << std::endl;

        vkMapMemory(vulkanDevice->getDevice(), gridUniformBuffersMemory[i], 0, bufferSize, 0, &gridUniformBuffersMapped[i]);
    }
}

void UniformBufferManager::createLightUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(LightUniformBufferObject); 
    
    lightBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    lightBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    lightBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        lightBuffers[i] = vulkanDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            lightBuffersMemory[i]);

        vkMapMemory(vulkanDevice->getDevice(), lightBuffersMemory[i], 0, bufferSize, 0, &lightBuffersMapped[i]);
    }
}

void UniformBufferManager::createSSAOKernelBuffers() {
    VkDeviceSize bufferSize = sizeof(SSAOKernelBufferObject);

    SSAOKernelBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    SSAOKernelBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    SSAOKernelBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        SSAOKernelBuffers[i] = vulkanDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            SSAOKernelBuffersMemory[i]);

        vkMapMemory(vulkanDevice->getDevice(), SSAOKernelBuffersMemory[i], 0, bufferSize, 0, &SSAOKernelBuffersMapped[i]);

        // Generate SSAO kernel samples (randomly distributed in the hemisphere)
        SSAOKernelBufferObject ssaoKernel;
        std::default_random_engine generator;
        std::uniform_real_distribution<float> randomFloats(0.0, 1.0);

        for (int j = 0; j < 16; ++j) {
            glm::vec3 sample(
                randomFloats(generator) * 2.0f - 1.0f,  // X in [-1, 1]
                randomFloats(generator) * 2.0f - 1.0f,  // Y in [-1, 1]
                randomFloats(generator)                   // Z in [0, 1]
            );
            sample = glm::normalize(sample);  // Normalize to unit length
            sample *= randomFloats(generator); // Scale with a random factor

            float scale = float(j) / 16.0f;
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            sample *= scale;

            ssaoKernel.SSAOKernel[j] = glm::vec4(sample, 0.0f);
        }
        // Copy the generated kernel to the mapped memory
        memcpy(SSAOKernelBuffersMapped[i], &ssaoKernel, sizeof(ssaoKernel));
    }
}

void UniformBufferManager::updateUniformBuffer(uint32_t currentImage, Camera& camera, UniformBufferObject& ubo) {
    
    ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    float constexpr angle =  glm::radians(90.0f);
    ubo.model = glm::rotate(ubo.model, angle, glm::vec3(0.0f, 1.0f, 0.0f));

    // (Camera position, look-at, and up vector)
    ubo.view = camera.getViewMatrix();

    ubo.proj = camera.getProjectionMatrix((float)swapChainExtent.width / (float)swapChainExtent.height);
    ubo.proj[1][1] *= -1;

    ubo.color = glm::vec3(0.044f, 0.04f, 0.044f);

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void UniformBufferManager::updateGridUniformBuffer(uint32_t currentImage,Camera& camera, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo) {
    
    // Grid ubo shares same matrices as main ubo   
    gridUbo.view = ubo.view;
    gridUbo.proj = ubo.proj;
    gridUbo.pos = glm::vec3(0.0f, 0.0f, 0.0f);

    memcpy(gridUniformBuffersMapped[currentImage], &gridUbo, sizeof(gridUbo));
}

void UniformBufferManager::updateLightUniformBuffer(uint32_t currentImage, Camera& camera, LightUniformBufferObject& lightUbo) {
    glm::vec3 cameraPosition = camera.getPosition();
    glm::vec3 cameraForward = camera.getForwardDirection();
    lightUbo.lightPos_Key = glm::vec3(0.0f, 2.0f, 1.0f);
    lightUbo.lightPos_Rim = cameraForward - cameraPosition;
    lightUbo.lightAmbient = glm::vec3(0.015f, 0.015f, 0.015f);
    memcpy(lightBuffersMapped[currentImage], &lightUbo, sizeof(lightUbo));
}

void UniformBufferManager::updateSSAOKernelBuffer(uint32_t currentImage, Camera& camera, SSAOKernelBufferObject& ssaoKernel) {
    memcpy(SSAOKernelBuffersMapped[currentImage], &ssaoKernel, sizeof(ssaoKernel));
}

void UniformBufferManager::cleanup() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->getDevice(), uniformBuffers[i], nullptr);

            std::cout << "Destroyed uniform buffer " << uniformBuffers[i] << std::endl;
        }

        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->getDevice(), uniformBuffersMemory[i], nullptr);

            std::cout << "Freed uniform buffer memory " << uniformBuffersMemory[i] << std::endl;
        }
        if (gridUniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->getDevice(), gridUniformBuffers[i], nullptr);

            std::cout << "Destroyed grid uniform buffer " << gridUniformBuffers[i] << std::endl;
        }

        if (gridUniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->getDevice(), gridUniformBuffersMemory[i], nullptr);

            std::cout << "Freed grid uniform buffer memory " << gridUniformBuffersMemory[i] << std::endl;
        }

        if (lightBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->getDevice(), lightBuffers[i], nullptr);

            std::cout << "Destroyed light buffer " << lightBuffers[i] << std::endl;
        }

        if (lightBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->getDevice(), lightBuffersMemory[i], nullptr);

            std::cout << "Freed light buffer memory " << lightBuffersMemory[i] << std::endl;
        }

        if (SSAOKernelBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->getDevice(), SSAOKernelBuffers[i], nullptr);
        }
        if (SSAOKernelBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->getDevice(), SSAOKernelBuffersMemory[i], nullptr);
        }
    }
}
