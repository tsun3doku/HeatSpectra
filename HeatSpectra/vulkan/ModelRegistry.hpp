#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

#include "runtime/RuntimeProducts.hpp"

class Model;
class MemoryAllocator;

class ModelRegistry {
public:
	ModelRegistry(MemoryAllocator& memoryAllocator);
	~ModelRegistry();

	ModelRegistry(const ModelRegistry&) = delete;
	ModelRegistry& operator=(const ModelRegistry&) = delete;
	void cleanup();
	void setModels(std::unique_ptr<Model> visModel, std::unique_ptr<Model> commonSubdivision, std::unique_ptr<Model> heatModel);
	uint32_t addModel(std::unique_ptr<Model> model, uint32_t preferredModelId = 0);
	bool removeModelByID(uint32_t modelID);
	std::vector<uint32_t> getRenderableModelIds() const;
	bool hasModel(uint32_t modelID) const;
	bool exportProduct(uint32_t modelID, ModelProduct& outProduct) const;
	bool setModelMatrix(uint32_t modelID, const glm::mat4& matrix);
	bool tryGetModelMatrix(uint32_t modelID, glm::mat4& outMatrix) const;
	bool tryGetBoundingBoxCenter(uint32_t modelID, glm::vec3& outCenter) const;
	bool tryGetBoundingBoxMinMax(uint32_t modelID, glm::vec3& outMin, glm::vec3& outMax) const;
	bool tryGetWorldBoundingBoxCenter(uint32_t modelID, glm::vec3& outCenter) const;

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
	Model* findModel(uint32_t modelID);
	const Model* findModel(uint32_t modelID) const;

	MemoryAllocator& memoryAllocator;

	std::unique_ptr<Model> visModel;
	std::unique_ptr<Model> commonSubdivision;
	std::unique_ptr<Model> heatModel;
	std::unordered_map<uint32_t, std::unique_ptr<Model>> additionalModelsById;
	std::unordered_map<uint32_t, Model*> modelsById;
	std::vector<uint32_t> recycledModelIds;
	uint32_t nextModelId = 1;
};

