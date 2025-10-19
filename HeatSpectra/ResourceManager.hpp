#pragma once

class iODT;
class SignpostMesh;
class Model;
class Grid;
class Camera;
class HeatSource;
class UniformBufferManager;
class MemoryAllocator;
class VulkanDevice;

const std::string MODEL_PATH	= "models/teapot.obj";
const std::string TEXTURE_PATH	= "textures/texture.jpg";

class CommandPool;

class ResourceManager {
public:
	ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, 
		VkRenderPass renderPass, Camera& camera, uint32_t maxFramesInFlight, 
		CommandPool* asyncCommandPool = nullptr, CommandPool* renderCommandPool = nullptr);
	~ResourceManager();

	// Delete copy operations
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	// Move operations
	ResourceManager(ResourceManager&&) noexcept;
	ResourceManager& operator=(ResourceManager&&) noexcept;

	void initialize();
	void performRemeshing(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize);
	void reloadModels(const std::string& modelPath);
	void cleanup();

	// Getters
	Grid& getGrid() {
		return *grid;
	}

	Model& getVisModel() {
		return *visModel;
	}
	Model& getHeatModel() {
		return *heatModel;
	}
	Model& getCommonSubdivision() {
		return *commonSubdivision;
	}

	HeatSource& getHeatSource() {
		return *heatSource;
	}

private:
	VulkanDevice& vulkanDevice;
	MemoryAllocator& memoryAllocator;
	UniformBufferManager& uniformBufferManager;
	Camera& camera;

	std::unique_ptr<Grid> grid;
	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> commonSubdivision;
	std::unique_ptr<Model> heatModel;
	std::unique_ptr<HeatSource> heatSource;

	std::unique_ptr<iODT> remesher;
	std::unique_ptr<SignpostMesh> signpostMesh;
};