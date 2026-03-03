#pragma once

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "scene/SceneView.hpp"
#include "util/Structs.hpp"

#include <vector>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>

class MemoryAllocator;
class VulkanDevice;

class UniformBufferManager {
public: 
	UniformBufferManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	~UniformBufferManager();

	void updateUniformBuffer(uint32_t currentImage, const render::SceneView& sceneView, UniformBufferObject& ubo);
	void setColor(glm::vec3 newColor, UniformBufferObject& ubo);

	void updateGridUniformBuffer(uint32_t currentImage, const render::SceneView& sceneView, GridUniformBufferObject& gridUbo, const glm::vec3& gridSize);
	void updateSSAOKernelBuffer(uint32_t currentImage, SSAOKernelBufferObject& ssaoKernel);

	void createUniformBuffers(uint32_t maxFramesInFlight);
	void createGridUniformBuffers(uint32_t maxFramesInFlight);
	void createLightUniformBuffers(uint32_t maxFramesInFlight);
	void createMaterialUniformBuffers(uint32_t maxFramesInFlight);
	void createSSAOKernelBuffers(uint32_t maxFramesInFlight);
	
	void cleanup(uint32_t maxFramesInFlight);

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

	const std::vector<VkBuffer>& getGridUniformBuffers() const {
		return gridUniformBuffers;
	}
	const std::vector<void*>& getGridUniformBuffersMapped() const {
		return gridUniformBuffersMapped;
	}
	const std::vector<VkDeviceSize>& getGridUniformBufferOffsets() const {
		return gridUniformBufferOffsets_;
	}

	const std::vector<VkBuffer>& getLightBuffers() const {
		return lightBuffers;
	}
	const std::vector<void*>& getLightBuffersMapped() const {
		return lightBuffersMapped;
	}	
	const std::vector<VkDeviceSize>& getLightBufferOffsets() const {
		return lightBufferOffsets_;
	}

	const std::vector<VkBuffer>& getMaterialBuffers() const {
		return materialBuffers;
	}
	const std::vector<void*>& getMaterialBuffersMapped() const {
		return materialBuffersMapped;
	}
	const std::vector<VkDeviceSize>& getMaterialBufferOffsets() const {
		return materialBufferOffsets_;
	}

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

	std::vector<VkBuffer> uniformBuffers;
	std::vector<void*> uniformBuffersMapped;
	std::vector<VkDeviceSize> uniformBufferOffsets_;

	std::vector<VkBuffer> gridUniformBuffers;
	std::vector<void*> gridUniformBuffersMapped;
	std::vector<VkDeviceSize> gridUniformBufferOffsets_;

	std::vector<VkBuffer> lightBuffers;
	std::vector<void*> lightBuffersMapped;
	std::vector<VkDeviceSize> lightBufferOffsets_;

	std::vector<VkBuffer> materialBuffers;
	std::vector<void*> materialBuffersMapped;
	std::vector<VkDeviceSize> materialBufferOffsets_;

	std::vector<VkBuffer> SSAOKernelBuffers;
	std::vector<void*> SSAOKernelBuffersMapped;
	std::vector<VkDeviceSize> SSAOKernelBufferOffsets_;
};
