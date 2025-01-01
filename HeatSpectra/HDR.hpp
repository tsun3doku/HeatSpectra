#pragma once

#include "VulkanDevice.hpp"
#include "CommandBufferManager.hpp"
#include "VulkanImage.hpp"
#include "Camera.hpp"

#include <glm/glm.hpp>
#include <string>

const std::string HDR_PATH = "C:/Users/tsundoku/Documents/Visual Studio 2022/Projects/HeatSpectra/HeatSpectra/textures/rainforest.hdr"; //change

class HDR {
public:
	HDR() = default;
	void init(VulkanDevice& vulkanDevice);
	void createHDRTextureImage(const std::string& filePath);
	void prefilterEnvMap(float roughness);
	void cleanup();

	VkSampler getEnvMapSampler() const {
		return envMapSampler;
	}
	VkImageView getEnvMapImageView() const {
		return envMapImageView;
	}
	VkImageView getFilteredEnvMapImageView() const {
		return filteredEnvMapImageView;
	}

	glm::vec3 sampleEnvironmentMap(const glm::vec3& direction, float roughness);
	glm::mat4 getCubemapViewMatrix(uint32_t face);

private:
	VulkanDevice* vulkanDevice;
	Camera* camera;

	VkImage envMapImage = VK_NULL_HANDLE;
	VkDeviceMemory envMapImageMemory = VK_NULL_HANDLE;
	VkImageView envMapImageView = VK_NULL_HANDLE;
	VkSampler envMapSampler = VK_NULL_HANDLE;

	VkImage filteredEnvImageMap = VK_NULL_HANDLE;
	VkDeviceMemory filteredEnvMapImageMemory = VK_NULL_HANDLE;
	VkImageView filteredEnvMapImageView = VK_NULL_HANDLE;

	VkRenderPass renderPass;
	VkFramebuffer framebuffer;
	std::vector<VkFramebuffer> framebuffers;
	VkImage cubemapImage;
	VkDeviceMemory cubemapImageMemory;
	VkImageView cubemapImageView;

	const uint32_t cubemapSize = 1024;
	const uint32_t mipLevels = 6;
	const uint32_t vertexCount = 6;

	void uploadHDRTextureData(float* data, VkDeviceMemory stagingBufferMemory, VkBuffer stagingBuffer, uint32_t texWidth, uint32_t texHeight);
	void createImage(VulkanDevice& vulkanDevice, const VkImageCreateInfo& createInfo, VkImage& image, VkDeviceMemory& imageMemory);
	void renderToCubemap(Camera& camera, VkCommandBuffer commandBuffer);
	void createCubemapImage();
	void createHDRRenderPass();
	void createHDRFrameBuffer();

}; 
