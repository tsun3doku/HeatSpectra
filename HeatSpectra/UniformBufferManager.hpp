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

class Camera;
class MemoryAllocator;
class VulkanDevice;

class UniformBufferManager {
public: 
	UniformBufferManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, uint32_t maxFramesInFlight);
	~UniformBufferManager();

	void updateUniformBuffer(VkExtent2D swapChainExtent, uint32_t currentImage, UniformBufferObject& ubo);
	void setColor(glm::vec3 newColor, UniformBufferObject& ubo);

	void updateGridUniformBuffer(uint32_t currentImage, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo);
	void updateLightUniformBuffer(uint32_t currentImage, LightUniformBufferObject& lightUbo);
	void updateSSAOKernelBuffer(uint32_t currentImage, SSAOKernelBufferObject& ssaoKernel);

	void createUniformBuffers(uint32_t maxFramesInFlight);
	void createGridUniformBuffers(uint32_t maxFramesInFlight);
	void createLightUniformBuffers(uint32_t maxFramesInFlight);
	void createSSAOKernelBuffers(uint32_t maxFramesInFlight);
	
	void cleanup(uint32_t maxFramesInFlight);

	// Getter functions for Main Uniform Buffers
	const std::vector<VkBuffer>& getUniformBuffers() const {
		return uniformBuffers;
	}
	const std::vector<void*>& getUniformBuffersMapped() const {
		return uniformBuffersMapped;
	}
	glm::vec3 getCurrentColor(UniformBufferObject& ubo) {
		return ubo.color;
	}
	const std::vector<VkDeviceSize>& getUniformBufferOffsets() const {
		return uniformBufferOffsets_;
	}

	// Getter functions for Grid Uniform Buffers
	const std::vector<VkBuffer>& getGridUniformBuffers() const {
		return gridUniformBuffers;
	}
	const std::vector<void*>& getGridUniformBuffersMapped() const {
		return gridUniformBuffersMapped;
	}
	const std::vector<VkDeviceSize>& getGridUniformBufferOffsets() const {
		return gridUniformBufferOffsets_;
	}

	// Getter functions for Light Buffers
	const std::vector<VkBuffer>& getLightBuffers() const {
		return lightBuffers;
	}
	const std::vector<void*>& getLightBuffersMapped() const {
		return lightBuffersMapped;
	}	
	const std::vector<VkDeviceSize>& getLightBufferOffsets() const {
		return lightBufferOffsets_;
	}

	// Getter functions for SSAO Kernel Buffers
	const std::vector<VkBuffer>& getSSAOKernelBuffers() const {
		return SSAOKernelBuffers;
	}
	const std::vector<void*>& getSSAOKernelBuffersMapped() const {
		return SSAOKernelBuffersMapped;
	}
	const std::vector<VkDeviceSize>& getSSAOKernelBufferOffsets() const {
		return SSAOKernelBufferOffsets_;
	}

private:	
	VulkanDevice& vulkanDevice;
	MemoryAllocator& memoryAllocator;
	Camera& camera;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<void*> uniformBuffersMapped;
	std::vector<VkDeviceSize> uniformBufferOffsets_;

	std::vector<VkBuffer> gridUniformBuffers;
	std::vector<void*> gridUniformBuffersMapped;
	std::vector<VkDeviceSize> gridUniformBufferOffsets_;

	std::vector<VkBuffer> lightBuffers;
	std::vector<void*> lightBuffersMapped;
	std::vector<VkDeviceSize> lightBufferOffsets_;

	std::vector<VkBuffer> SSAOKernelBuffers;
	std::vector<void*> SSAOKernelBuffersMapped;
	std::vector<VkDeviceSize> SSAOKernelBufferOffsets_;
};