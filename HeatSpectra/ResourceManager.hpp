#pragma once

class Model;
class Grid;
class HeatSource;
class HeatSystem;
class UniformBufferManager;
class MemoryAllocator;
class VulkanDevice;

class ResourceManager {
public:
	ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, VkRenderPass renderPass, uint32_t maxFramesInFlight);
	~ResourceManager();

	// Delete copy operations
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	// Move operations
	ResourceManager(ResourceManager&&) noexcept;
	ResourceManager& operator=(ResourceManager&&) noexcept;

	void initialize();
	void cleanup();

	// Getters
	Grid& getGrid() {
		return *grid;
	}

	Model& getSimModel() {
		return *simModel;
	}
	Model& getVisModel() {
		return *visModel;
	}
	Model& getHeatModel() {
		return *heatModel;
	}

	HeatSource& getHeatSource() {
		return *heatSource;
	}
	HeatSystem& getHeatSystem() {
		return *heatSystem;
	}

private:
	VulkanDevice& vulkanDevice;
	MemoryAllocator& memoryAllocator;
	UniformBufferManager& uniformBufferManager;

	std::unique_ptr<Grid> grid;
	std::unique_ptr<Model> simModel;
	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> heatModel;
	std::unique_ptr<HeatSource> heatSource;
	std::unique_ptr<HeatSystem> heatSystem;
};