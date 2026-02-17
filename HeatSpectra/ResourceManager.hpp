#pragma once

#include <vulkan/vulkan.h>

#include "iODT.hpp"
#include "CommonSubdivision.hpp"
#include <vector>
#include <unordered_map>
#include <memory>

class Model;
class Grid;
class TimingOverlay;
class Camera;
class HeatSource;
class SurfelRenderer;
class UniformBufferManager;
class MemoryAllocator;
class VulkanDevice;

const std::string MODEL_PATH	= "models/teapot.obj";
const std::string TEXTURE_PATH	= "textures/texture.jpg";

class CommandPool;

struct RemeshData {
    std::unique_ptr<iODT> remesher;
    std::unique_ptr<SurfelRenderer> surfel; 
    bool isRemeshed = false;
    
    RemeshData() = default;
    RemeshData(RemeshData&&) = default;
    RemeshData& operator=(RemeshData&&) = default;
    
    RemeshData(const RemeshData&) = delete;
    RemeshData& operator=(const RemeshData&) = delete;
};

class ResourceManager {
public:
	ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, 
		VkRenderPass renderPass, Camera& camera, uint32_t maxFramesInFlight,
        uint32_t overlaySubpass = 2,
		CommandPool* asyncCommandPool = nullptr, CommandPool* renderCommandPool = nullptr);
	~ResourceManager();

	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	ResourceManager(ResourceManager&&) noexcept;
	ResourceManager& operator=(ResourceManager&&) noexcept;

	void initialize();
	
	// Remeshing operations
	void performRemeshing(Model* targetModel, int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize,
	                      CommandPool& cmdPool, uint32_t maxFramesInFlight);
	void performRemeshingOnSelected(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize,
	                                CommandPool& cmdPool, uint32_t maxFramesInFlight);
	
	void reloadModels(const std::string& modelPath);
	void cleanup();

    void buildCommonSubdivision(bool enable = false);

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
	
	// Model selection
	void setSelectedModel(Model* model) { selectedModel = model; }
	Model* getSelectedModel() const { return selectedModel; }
	
	// Model ID mapping 
	Model* getModelByID(uint32_t modelID);
	uint32_t getModelID(Model* model) const;
	
	// Per model queries
	bool isModelRemeshed(Model* model) const;
	bool areRequiredModelsRemeshed() const; 
	iODT* getRemesherForModel(Model* model);
    SurfelRenderer* getSurfelPerModel(Model* model);

	const std::unordered_map<Model*, RemeshData>& getModelRemeshData() const { return modelRemeshData; }
	const std::vector<CommonSubdivision::IntrinsicTriangle>& getIntrinsicTriangles() const { return intrinsicTriangles; }
	
	glm::vec3 calculateMaxBoundingBoxSize() const;
    void updateTimingOverlayText(const std::vector<std::string>& lines);
    void renderTimingOverlay(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkExtent2D extent);

private:
	VulkanDevice& vulkanDevice;
	MemoryAllocator& memoryAllocator;
	UniformBufferManager& uniformBufferManager;
	Camera& camera;

	std::unique_ptr<Grid> grid;
	std::unique_ptr<TimingOverlay> timingOverlay;
	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> commonSubdivision;
	std::unique_ptr<Model> heatModel;
	std::unique_ptr<HeatSource> heatSource;

	// Per model
	std::unordered_map<Model*, RemeshData> modelRemeshData;	
	Model* selectedModel = nullptr;	
	std::vector<CommonSubdivision::IntrinsicTriangle> intrinsicTriangles;
};
