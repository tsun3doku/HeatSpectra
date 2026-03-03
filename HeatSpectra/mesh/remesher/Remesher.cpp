#include "Remesher.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "scene/Model.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>
#include <memory>

Remesher::Remesher(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      uniformBufferManager(uniformBufferManager) {
}

bool Remesher::performRemeshing(Model* targetModel, int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize) {
    if (!targetModel) {
        std::cerr << "[Remesher] Cannot remesh null model" << std::endl;
        return false;
    }

    ModelRemeshData& remeshData = modelRemeshData[targetModel];
    remeshData.remesher = std::make_unique<iODT>(*targetModel, vulkanDevice, memoryAllocator);

    const bool success = remeshData.remesher->optimalDelaunayTriangulation(
        iterations,
        minAngleDegrees,
        maxEdgeLength,
        stepSize);
    if (!success) {
        std::cerr << "[Remesher] Remeshing failed for model" << std::endl;
        return false;
    }

    remeshData.isRemeshed = true;

    auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
    if (supportingHalfedge) {
        remeshData.surfel = std::make_unique<SurfelRenderer>(
            vulkanDevice,
            memoryAllocator,
            uniformBufferManager);
    }

    return true;
}

bool Remesher::isModelRemeshed(const Model* model) const {
    if (!model) {
        return false;
    }

    auto it = modelRemeshData.find(const_cast<Model*>(model));
    return it != modelRemeshData.end() && it->second.isRemeshed;
}

bool Remesher::areAllModelsRemeshed(const Model* heatModel, const Model* visModel) const {
    return isModelRemeshed(heatModel) && isModelRemeshed(visModel);
}

iODT* Remesher::getRemesherForModel(Model* model) {
    if (!model) {
        return nullptr;
    }

    auto it = modelRemeshData.find(model);
    if (it == modelRemeshData.end()) {
        return nullptr;
    }

    return it->second.remesher.get();
}

const iODT* Remesher::getRemesherForModel(const Model* model) const {
    if (!model) {
        return nullptr;
    }

    auto it = modelRemeshData.find(const_cast<Model*>(model));
    if (it == modelRemeshData.end()) {
        return nullptr;
    }

    return it->second.remesher.get();
}

void Remesher::removeModel(Model* model) {
    if (!model) {
        return;
    }

    auto it = modelRemeshData.find(model);
    if (it == modelRemeshData.end()) {
        return;
    }

    if (it->second.remesher) {
        it->second.remesher->cleanup();
    }

    modelRemeshData.erase(it);
}

SurfelRenderer* Remesher::getSurfelForModel(Model* model) {
    if (!model) {
        return nullptr;
    }

    auto it = modelRemeshData.find(model);
    if (it == modelRemeshData.end()) {
        return nullptr;
    }

    return it->second.surfel.get();
}

const SurfelRenderer* Remesher::getSurfelForModel(const Model* model) const {
    if (!model) {
        return nullptr;
    }

    auto it = modelRemeshData.find(const_cast<Model*>(model));
    if (it == modelRemeshData.end()) {
        return nullptr;
    }

    return it->second.surfel.get();
}

std::vector<Model*> Remesher::getRemeshedModels() const {
    std::vector<Model*> models;
    models.reserve(modelRemeshData.size());

    for (const auto& [model, remeshData] : modelRemeshData) {
        if (remeshData.isRemeshed && remeshData.remesher) {
            models.push_back(model);
        }
    }

    return models;
}

void Remesher::cleanup() {
    for (auto& [model, remeshData] : modelRemeshData) {
        (void)model;
        if (remeshData.remesher) {
            remeshData.remesher->cleanup();
        }
    }

    modelRemeshData.clear();
}
