#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

class Model;
class MemoryAllocator;

class ResourceManager {
public:
	ResourceManager(MemoryAllocator& memoryAllocator);
	~ResourceManager();

	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;
	void cleanup();
	void setModels(std::unique_ptr<Model> visModel, std::unique_ptr<Model> commonSubdivision, std::unique_ptr<Model> heatModel);
	uint32_t addModel(std::unique_ptr<Model> model, uint32_t preferredModelId = 0);
	bool removeModelByID(uint32_t modelID);
	std::vector<uint32_t> getRenderableModelIds() const;

	// Getters
	Model& getVisModel() {
		return *visModel;
	}
	Model& getHeatModel() {
		return *heatModel;
	}
	Model& getCommonSubdivision() {
		return *commonSubdivision;
	}

	MemoryAllocator& getMemoryAllocator() {
		return memoryAllocator;
	}

	// Model ID mapping 
	Model* getModelByID(uint32_t modelID);
	const Model* getModelByID(uint32_t modelID) const;
	uint32_t getModelID(Model* model) const;
	uint32_t getVisModelID() const;
	uint32_t getHeatModelID() const;
	uint32_t getCommonSubdivisionModelID() const;
	
	glm::vec3 calculateMaxBoundingBoxSize() const;

private:
	static constexpr uint32_t MaxStencilModelId = 255u;
	static bool isReservedModelId(uint32_t modelId);
	uint32_t acquireModelId(uint32_t preferredModelId = 0);
	void recycleModelId(uint32_t modelId);
	void registerModel(std::unique_ptr<Model>& modelSlot, uint32_t preferredModelId = 0);
	void unregisterModel(std::unique_ptr<Model>& modelSlot);
	void clearAdditionalModels();

	MemoryAllocator& memoryAllocator;

	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> commonSubdivision;
	std::unique_ptr<Model> heatModel;
	std::unordered_map<uint32_t, std::unique_ptr<Model>> additionalModelsById;
	std::unordered_map<uint32_t, Model*> modelsById;
	std::vector<uint32_t> recycledModelIds;
	uint32_t nextModelId = 1;
};
