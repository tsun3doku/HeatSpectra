#ifndef UNIFORM_BUFFER_UTILS_HPP
#define UNIFORM_BUFFER_UTILS_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <vector>
#include "Structs.hpp"
#include "vulkan_device.hpp"


class UniformBufferManager {
public: 
	UniformBufferManager() = default;

	void init(VulkanDevice& vulkanDevice, VkExtent2D swapChainExtent);
	void updateUniformBuffer(uint32_t currentImage, UniformBufferObject& ubo);
	void updateGridUniformBuffer(uint32_t currentImage, const UniformBufferObject& ubo, GridUniformBufferObject& gridUbo);

	void createUniformBuffers();
	void createGridUniformBuffers();
	
	void cleanup();

	std::vector<VkBuffer>& getUniformBuffers() {
		return uniformBuffers;
}

	std::vector<VkBuffer>& getGridUniformBuffers() {
		return gridUniformBuffers;
}

	std::vector<void*>& getUniformBuffersMapped() {
		return uniformBuffersMapped;
}

	std::vector<void*>& getGridUniformBuffersMapped() {
		return gridUniformBuffersMapped;
}
	std::vector<VkDeviceMemory>& getUniformBuffersMemory() {
		return uniformBuffersMemory;
	}

	std::vector<VkDeviceMemory>& getGridUniformBuffersMemory() {
		return gridUniformBuffersMemory;
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
	
};
#endif