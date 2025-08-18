#pragma once

class iODT;
class SignpostMesh;
class Model;
class Grid;
class Camera;
class HeatSource;
class HeatSystem;
class UniformBufferManager;
class MemoryAllocator;
class VulkanDevice;

const std::string MODEL_PATH	= "models/teapot.obj";
const std::string TEXTURE_PATH	= "textures/texture.jpg";

class ResourceManager {
public:
	ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, 
		VkRenderPass renderPass, Camera& camera, uint32_t maxFramesInFlight);
	~ResourceManager();

	// Delete copy operations
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	// Move operations
	ResourceManager(ResourceManager&&) noexcept;
	ResourceManager& operator=(ResourceManager&&) noexcept;

	void initialize();
	void initializeRemesher();
	void performRemeshing(float targetEdgeLength, int iterations);
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
	Camera& camera;

	std::unique_ptr<Grid> grid;
	std::unique_ptr<Model> simModel;
	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> heatModel;
	std::unique_ptr<HeatSource> heatSource;
	std::unique_ptr<HeatSystem> heatSystem;

	std::unique_ptr<iODT> remesher;
	std::unique_ptr<SignpostMesh> signpostMesh;
};