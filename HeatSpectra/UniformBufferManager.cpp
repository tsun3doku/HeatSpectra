#include <vulkan/vulkan.h>

#include "Camera.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "UniformBufferManager.hpp"

UniformBufferManager::UniformBufferManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight)
: vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator) {
    createUniformBuffers(memoryAllocator, maxFramesInFlight);
    createGridUniformBuffers(memoryAllocator, maxFramesInFlight);
    createLightUniformBuffers(memoryAllocator, maxFramesInFlight);
    createSSAOKernelBuffers(memoryAllocator, maxFramesInFlight);
}

UniformBufferManager::~UniformBufferManager() {
}

void UniformBufferManager::createUniformBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    uniformBuffers.resize(maxFramesInFlight);
    uniformBuffersMapped.resize(maxFramesInFlight);
    uniformBufferOffsets_.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        auto [buffer, offset] = memoryAllocator.allocate(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
        );

        uniformBuffers[i] = buffer;
        uniformBufferOffsets_[i] = offset;
        uniformBuffersMapped[i] = memoryAllocator.getMappedPointer(buffer, offset);
    }
}

void UniformBufferManager::createGridUniformBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    VkDeviceSize bufferSize = sizeof(GridUniformBufferObject);

    gridUniformBuffers.resize(maxFramesInFlight);
    gridUniformBuffersMapped.resize(maxFramesInFlight);
    gridUniformBufferOffsets_.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        auto [buffer, offset] = memoryAllocator.allocate(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
        );

        gridUniformBuffers[i] = buffer;
        gridUniformBufferOffsets_[i] = offset;
        gridUniformBuffersMapped[i] = memoryAllocator.getMappedPointer(buffer, offset);
    }
}

void UniformBufferManager::createLightUniformBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    VkDeviceSize bufferSize = sizeof(LightUniformBufferObject); 
    
    lightBuffers.resize(maxFramesInFlight);
    lightBuffersMapped.resize(maxFramesInFlight);
    lightBufferOffsets_.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        auto [buffer, offset] = memoryAllocator.allocate(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
        );

        lightBuffers[i] = buffer;
        lightBufferOffsets_[i] = offset;
        lightBuffersMapped[i] = memoryAllocator.getMappedPointer(buffer, offset);
    }
}

void UniformBufferManager::createSSAOKernelBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    VkDeviceSize bufferSize = sizeof(SSAOKernelBufferObject);

    SSAOKernelBuffers.resize(maxFramesInFlight);
    SSAOKernelBuffersMapped.resize(maxFramesInFlight);
    SSAOKernelBufferOffsets_.resize(maxFramesInFlight);

    // Generate SSAO kernel samples once
    SSAOKernelBufferObject ssaoKernel;
    std::default_random_engine generator;
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);

    for (int j = 0; j < 16; ++j) {
        glm::vec3 sample(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator)
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);

        float scale = float(j) / 16.0f;
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        sample *= scale;

        ssaoKernel.SSAOKernel[j] = glm::vec4(sample, 0.0f);
    }

    // Create buffers and copy kernel data
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        auto [buffer, offset] = memoryAllocator.allocate(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
        );

        SSAOKernelBuffers[i] = buffer;
        SSAOKernelBufferOffsets_[i] = offset;
        SSAOKernelBuffersMapped[i] = memoryAllocator.getMappedPointer(buffer, offset);

        memcpy(SSAOKernelBuffersMapped[i], &ssaoKernel, sizeof(ssaoKernel));
    }
}

void UniformBufferManager::updateUniformBuffer(VkExtent2D swapChainExtent, uint32_t currentImage, Camera& camera, UniformBufferObject& ubo) {
    // Get current time
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    // Create rotation matrix with time-based angle
    ubo.model = glm::mat4(1.0f);
    ubo.model = glm::rotate(ubo.model, time * glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Camera matrices remain the same
    ubo.view = camera.getViewMatrix();
    ubo.proj = camera.getProjectionMatrix((float)swapChainExtent.width / (float)swapChainExtent.height);
    ubo.proj[1][1] *= -1;

    ubo.color = glm::vec3(0.044f, 0.044f, 0.044f);

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void UniformBufferManager::setColor(glm::vec3 newColor, UniformBufferObject& ubo) {
    ubo.color = newColor;
    std::cout << "Color updated to: " << newColor.x << ", " << newColor.y << ", " << newColor.z << std::endl;
    for (size_t i = 0; i < uniformBuffersMapped.size(); i++) {
        UniformBufferObject* mappedUbo = (UniformBufferObject*)uniformBuffersMapped[i];
        mappedUbo->color = newColor;
    }
}

void UniformBufferManager::updateGridUniformBuffer(uint32_t currentImage,Camera& camera, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo) {
    
    // Grid ubo shares same matrices as main ubo   
    gridUbo.view = ubo.view;
    gridUbo.proj = ubo.proj;
    gridUbo.pos = camera.getPosition();

    memcpy(gridUniformBuffersMapped[currentImage], &gridUbo, sizeof(gridUbo));
}

void UniformBufferManager::updateLightUniformBuffer(uint32_t currentImage, Camera& camera, LightUniformBufferObject& lightUbo) {
    glm::vec3 cameraPosition = camera.getPosition();
    glm::vec3 cameraForward = camera.getForwardDirection();
    lightUbo.lightPos_Key = glm::vec3(0.0f, 2.0f, 0.0f);
    lightUbo.lightPos_Rim = cameraForward - cameraPosition;
    lightUbo.lightAmbient = glm::vec3(0.01f, 0.01f, 0.01f);
    memcpy(lightBuffersMapped[currentImage], &lightUbo, sizeof(lightUbo));
}

void UniformBufferManager::updateSSAOKernelBuffer(uint32_t currentImage, Camera& camera, SSAOKernelBufferObject& ssaoKernel) {
    memcpy(SSAOKernelBuffersMapped[currentImage], &ssaoKernel, sizeof(ssaoKernel));
}

void UniformBufferManager::cleanup(uint32_t maxFramesInFlight) {
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(uniformBuffers[i], 0);
            std::cout << "Freed main uniform buffers " << uniformBuffers[i] << std::endl;
        }

        if (gridUniformBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(gridUniformBuffers[i], 0);
            std::cout << "Freed grid uniform buffers " << gridUniformBuffers[i] << std::endl;
        }

        if (lightBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(lightBuffers[i], 0);
            std::cout << "Freed light uniform buffers " << lightBuffers[i] << std::endl;
        }

        if (SSAOKernelBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(SSAOKernelBuffers[i], 0);
            std::cout << "Freed SSAO kernel uniform buffers " << SSAOKernelBuffers[i] << std::endl;
        }
    }
}

