#ifndef UNIFORM_BUFFER_UTILS_HPP
#define UNIFORM_BUFFER_UTILS_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <vector>
#include "Structs.hpp"
#include "vulkan_device.hpp"
#include "Camera.hpp"


class UniformBufferManager {
public: 
	UniformBufferManager() = default;

	void init(VulkanDevice& vulkanDevice, VkExtent2D swapChainExtent);
	void updateUniformBuffer(uint32_t currentImage, Camera& camera, UniformBufferObject& ubo);
	void updateGridUniformBuffer(uint32_t currentImage, Camera& camera, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo);
	void updateLightUniformBuffer(uint32_t currentImage, Camera& camera, LightUniformBufferObject& lightUbo);

	void createUniformBuffers();
	void createGridUniformBuffers();
	void createLightUniformBuffers();
	
	void cleanup();

	std::vector<VkBuffer>& getUniformBuffers() {
		return uniformBuffers;
	}

	std::vector<VkBuffer>& getGridUniformBuffers() {
		return gridUniformBuffers;
	}

	std::vector<VkBuffer>& getLightBuffers() {
		return lightBuffers;
	}

	std::vector<void*>& getUniformBuffersMapped() {
		return uniformBuffersMapped;
	}

	std::vector<void*>& getGridUniformBuffersMapped() {
		return gridUniformBuffersMapped;
	}

	std::vector<void*>& getLightBuffersMapped() {
		return lightBuffersMapped;
	}

	std::vector<VkDeviceMemory>& getUniformBuffersMemory() {
		return uniformBuffersMemory;
	}

	std::vector<VkDeviceMemory>& getGridUniformBuffersMemory() {
		return gridUniformBuffersMemory;
	}

	std::vector<VkDeviceMemory>& getLightBuffersMemory() {
		return lightBuffersMemory;
	}
	
private:
	const int MAX_FRAMES_IN_FLIGHT = 2;
	
	VulkanDevice* vulkanDevice;
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
	
};
#endif