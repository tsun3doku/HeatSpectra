#pragma once

class Model;
class HeatSource;
class HeatSystem;
class UniformBufferManager;
class MemoryAllocator;
class VulkanDevice;

class ResourceManager {
public:
	ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
	~ResourceManager();

	// Delete copy operations
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	// Move operations
	ResourceManager(ResourceManager&&) noexcept;
	ResourceManager& operator=(ResourceManager&&) noexcept;

	// Getters
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

	UniformBufferManager& getUniformBufferManager() { 
		return *uniformBufferManager; 
	}

	void initialize();

private:
	VulkanDevice& vulkanDevice;
	MemoryAllocator& memoryAllocator;

	std::unique_ptr<Model> simModel;
	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> heatModel;
	std::unique_ptr<HeatSource> heatSource;
	std::unique_ptr<HeatSystem> heatSystem;
	std::unique_ptr<UniformBufferManager> uniformBufferManager;
};