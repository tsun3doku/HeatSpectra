#pragma once

#include "iODT.hpp"
#include "renderers/SurfelRenderer.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

class Model;
class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;

class Remesher {
public:
    Remesher(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager);

    bool performRemeshing(Model* targetModel, int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize);

    bool isModelRemeshed(const Model* model) const;
    bool areAllModelsRemeshed(const Model* heatModel, const Model* visModel) const;

    iODT* getRemesherForModel(Model* model);
    const iODT* getRemesherForModel(const Model* model) const;
    void removeModel(Model* model);

    SurfelRenderer* getSurfelForModel(Model* model);
    const SurfelRenderer* getSurfelForModel(const Model* model) const;

    std::vector<Model*> getRemeshedModels() const;

    void cleanup();

private:
    struct ModelRemeshData {
        std::unique_ptr<iODT> remesher;
        std::unique_ptr<SurfelRenderer> surfel;
        bool isRemeshed = false;

        ModelRemeshData() = default;
        ModelRemeshData(ModelRemeshData&&) = default;
        ModelRemeshData& operator=(ModelRemeshData&&) = default;
        ModelRemeshData(const ModelRemeshData&) = delete;
        ModelRemeshData& operator=(const ModelRemeshData&) = delete;
    };

    std::unordered_map<Model*, ModelRemeshData> modelRemeshData;

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
};
