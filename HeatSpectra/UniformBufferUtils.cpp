#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "UniformBufferUtils.hpp"
#include <chrono>
#include <cstring>
#include <iostream>


void UniformBufferManager::init(VulkanDevice& vulkanDevice, VkExtent2D swapChainExtent) {
    this->vulkanDevice = &vulkanDevice; //reference to VulkanDevice
    this->swapChainExtent = swapChainExtent;

    std::cout << "Logical device in UniformBufferManager: " << vulkanDevice.getDevice() << std::endl;
    
    createUniformBuffers();
    createGridUniformBuffers();
 
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
    }
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

void UniformBufferManager::updateUniformBuffer(uint32_t currentImage, UniformBufferObject& ubo) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    //time dependent rotation 
    ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    float angle = time * glm::radians(90.0f);
    ubo.model = glm::rotate(ubo.model, angle, glm::vec3(0.0f, 1.0f, 0.0f));

    //(camera position, look-at, and up vector)
    ubo.view = glm::lookAt(glm::vec3(1.0f, 1.0f, -2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    ubo.proj = glm::perspective(glm::radians(40.0f), (float)(swapChainExtent.width / -(float)swapChainExtent.height), 0.05f, 100.0f); // flip height
    ubo.proj[1][1] *= -1; 

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void UniformBufferManager::updateGridUniformBuffer(uint32_t currentImage, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo) {
    //grid ubo shares same matrices as main ubo
   
    gridUbo.view = ubo.view;
    gridUbo.proj = ubo.proj;
    gridUbo.pos = glm::vec3(0.0f, 0.0f, 0.0f);

    memcpy(gridUniformBuffersMapped[currentImage], &gridUbo, sizeof(gridUbo));
}
