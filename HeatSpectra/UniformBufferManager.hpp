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
	UniformBufferManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	~UniformBufferManager();

	void updateUniformBuffer(VkExtent2D swapChainExtent, uint32_t currentImage, Camera& camera, UniformBufferObject& ubo);
	void setColor(glm::vec3 newColor, UniformBufferObject& ubo);

	void updateGridUniformBuffer(uint32_t currentImage, Camera& camera, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo);
	void updateLightUniformBuffer(uint32_t currentImage, Camera& camera, LightUniformBufferObject& lightUbo);
	void updateSSAOKernelBuffer(uint32_t currentImage, Camera& camera, SSAOKernelBufferObject& ssaoKernel);

	void createUniformBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	void createGridUniformBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	void createLightUniformBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	void createSSAOKernelBuffers(MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	
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
	const VkDeviceSize& getUniformBufferOffset() const {
		return uniformBufferOffset_;
	}

	// Getter functions for Grid Uniform Buffers
	const std::vector<VkBuffer>& getGridUniformBuffers() const {
		return gridUniformBuffers;
	}
	const std::vector<void*>& getGridUniformBuffersMapped() const {
		return gridUniformBuffersMapped;
	}
	const VkDeviceSize& getGridUniformBufferOffset() const {
		return gridUniformBufferOffset_;
	}

	// Getter functions for Light Buffers
	const std::vector<VkBuffer>& getLightBuffers() const {
		return lightBuffers;
	}
	const std::vector<void*>& getLightBuffersMapped() const {
		return lightBuffersMapped;
	}	
	const VkDeviceSize& getLightBufferOffset() const {
		return lightBufferOffset_;
	}

	// Getter functions for SSAO Kernel Buffers
	const std::vector<VkBuffer>& getSSAOKernelBuffers() const {
		return SSAOKernelBuffers;
	}
	const std::vector<void*>& getSSAOKernelBuffersMapped() const {
		return SSAOKernelBuffersMapped;
	}

private:	
	VulkanDevice& vulkanDevice;
	MemoryAllocator& memoryAllocator;
	Camera* camera = nullptr;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<void*> uniformBuffersMapped;
	VkDeviceSize uniformBufferOffset_;

	std::vector<VkBuffer> gridUniformBuffers;
	std::vector<void*> gridUniformBuffersMapped;
	VkDeviceSize gridUniformBufferOffset_;

	std::vector<VkBuffer> lightBuffers;
	std::vector<void*> lightBuffersMapped;
	VkDeviceSize lightBufferOffset_;

	std::vector<VkBuffer> SSAOKernelBuffers;
	std::vector<void*> SSAOKernelBuffersMapped;	
	VkDeviceSize SSAOKernelBufferOffset_;
};