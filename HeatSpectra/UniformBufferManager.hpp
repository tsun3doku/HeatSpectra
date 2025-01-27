#pragma once

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Structs.hpp"

#include <vector>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>

class VulkanDevice;
class Camera;

class UniformBufferManager {
public: 
	UniformBufferManager() = default;

	void init(VulkanDevice& vulkanDevice, VkExtent2D swapChainExtent, uint32_t maxFramesInFlight);

	void updateUniformBuffer(uint32_t currentImage, Camera& camera, UniformBufferObject& ubo);
	void setColor(glm::vec3 newColor, UniformBufferObject& ubo);

	void updateGridUniformBuffer(uint32_t currentImage, Camera& camera, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo);
	void updateLightUniformBuffer(uint32_t currentImage, Camera& camera, LightUniformBufferObject& lightUbo);
	void updateSSAOKernelBuffer(uint32_t currentImage, Camera& camera, SSAOKernelBufferObject& ssaoKernel);

	void createUniformBuffers(uint32_t maxFramesInFlight);
	void createGridUniformBuffers(uint32_t maxFramesInFlight);
	void createLightUniformBuffers(uint32_t maxFramesInFlight);
	void createSSAOKernelBuffers(uint32_t maxFramesInFlight); //new
	
	void cleanup(uint32_t maxFramesInFlight);

	// Getter functions for Main Uniform Buffers
	const std::vector<VkBuffer>& getUniformBuffers() const {
		return uniformBuffers;
	}
	const std::vector<void*>& getUniformBuffersMapped() const {
		return uniformBuffersMapped;
	}
	const std::vector<VkDeviceMemory>& getUniformBuffersMemory() const {
		return uniformBuffersMemory;
	}
	glm::vec3 getCurrentColor(UniformBufferObject& ubo) {
		return ubo.color;
	}

	// Getter functions for Grid Uniform Buffers
	const std::vector<VkBuffer>& getGridUniformBuffers() const {
		return gridUniformBuffers;
	}
	const std::vector<void*>& getGridUniformBuffersMapped() const {
		return gridUniformBuffersMapped;
	}
	const std::vector<VkDeviceMemory>& getGridUniformBuffersMemory() const {
		return gridUniformBuffersMemory;
	}

	// Getter functions for Light Buffers
	const std::vector<VkBuffer>& getLightBuffers() const {
		return lightBuffers;
	}
	const std::vector<void*>& getLightBuffersMapped() const {
		return lightBuffersMapped;
	}
	const std::vector<VkDeviceMemory>& getLightBuffersMemory() const {
		return lightBuffersMemory;
	}

	// Getter functions for SSAO Kernel Buffers
	const std::vector<VkBuffer>& getSSAOKernelBuffers() const {
		return SSAOKernelBuffers;
	}
	const std::vector<void*>& getSSAOKernelBuffersMapped() const {
		return SSAOKernelBuffersMapped;
	}
	const std::vector<VkDeviceMemory>& getSSAOKernelBuffersMemory() const {
		return SSAOKernelBuffersMemory;
	}
	
private:	
	VulkanDevice* vulkanDevice;
	Camera* camera;

	VkExtent2D swapChainExtent;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;
	std::vector<void*> uniformBuffersMapped;

	std::vector<VkBuffer> gridUniformBuffers;
	std::vector<VkDeviceMemory> gridUniformBuffersMemory;
	std::vector<void*> gridUniformBuffersMapped;

	std::vector<VkBuffer> lightBuffers;
	std::vector<VkDeviceMemory> lightBuffersMemory;
	std::vector<void*> lightBuffersMapped;

	std::vector<VkBuffer> SSAOKernelBuffers;
	std::vector<VkDeviceMemory> SSAOKernelBuffersMemory;
	std::vector<void*> SSAOKernelBuffersMapped;	
};