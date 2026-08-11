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
	uint32_t addModel(std::unique_ptr<Model> model, uint32_t preferredModelId = 0);
	bool removeModelByID(uint32_t modelID);
	std::vector<uint32_t> getRenderableModelIds() const;
	bool hasModel(uint32_t modelID) const;
	bool setModelVisible(uint32_t modelID, bool visible);
	bool isModelVisible(uint32_t modelID) const;
	bool exportProduct(uint32_t modelID, ModelProduct& outProduct) const;
	bool tryGetRenderGeometry(
		uint32_t modelID,
		VkBuffer& outVertexBuffer,
		VkDeviceSize& outVertexBufferOffset,
		VkBuffer& outIndexBuffer,
		VkDeviceSize& outIndexBufferOffset,
		uint32_t& outIndexCount) const;
	bool setModelMatrix(uint32_t modelID, const glm::mat4& matrix);
	bool tryGetModelMatrix(uint32_t modelID, glm::mat4& outMatrix) const;
	bool tryGetLocalBounds(uint32_t modelID, glm::vec3& outMin, glm::vec3& outMax) const;
	bool tryGetWorldBounds(uint32_t modelID, glm::vec3& outMin, glm::vec3& outMax) const;
	glm::vec3 sceneWorldExtent() const;

	MemoryAllocator& getMemoryAllocator() {
		return memoryAllocator;
	}

private:
	static constexpr uint32_t MaxStencilModelId = 255u;
	static bool isReservedModelId(uint32_t modelId);
	uint32_t acquireModelId(uint32_t preferredModelId = 0);
	void recycleModelId(uint32_t modelId);
	void unregisterModel(uint32_t modelID);
	void clearModels();
	Model* findModel(uint32_t modelID);
	const Model* findModel(uint32_t modelID) const;

	MemoryAllocator& memoryAllocator;

	std::unordered_map<uint32_t, std::unique_ptr<Model>> models;
	std::unordered_set<uint32_t> visibleModelIds;
	std::vector<uint32_t> recycledModelIds;
	uint32_t nextModelId = 1;
};
