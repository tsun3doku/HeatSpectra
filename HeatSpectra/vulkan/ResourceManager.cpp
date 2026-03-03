#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

#include "MemoryAllocator.hpp"
#include "scene/Model.hpp"
#include "ResourceManager.hpp"

ResourceManager::ResourceManager(MemoryAllocator& memoryAllocator)
    : memoryAllocator(memoryAllocator) {
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::clearAdditionalModels() {
    for (auto& [modelId, model] : additionalModelsById) {
        (void)modelId;
        if (model) {
            model->cleanup();
        }
        unregisterModel(model);
    }
    additionalModelsById.clear();
}

void ResourceManager::setModels(std::unique_ptr<Model> newVisModel, std::unique_ptr<Model> newCommonSubdivision, std::unique_ptr<Model> newHeatModel) {
    const uint32_t visModelId = getVisModelID();
    const uint32_t heatModelId = getHeatModelID();
    const uint32_t commonSubdivisionId = getCommonSubdivisionModelID();

    clearAdditionalModels();

    unregisterModel(visModel);
    unregisterModel(heatModel);
    unregisterModel(commonSubdivision);

    visModel = std::move(newVisModel);
    heatModel = std::move(newHeatModel);
    commonSubdivision = std::move(newCommonSubdivision);

    // Keep renderable IDs in the low stencil-safe range.
    registerModel(visModel, visModelId);
    registerModel(heatModel, heatModelId);
    registerModel(commonSubdivision, commonSubdivisionId);
}

uint32_t ResourceManager::addModel(std::unique_ptr<Model> model, uint32_t preferredModelId) {
    if (!model) {
        return 0;
    }

    const uint32_t modelId = acquireModelId(preferredModelId);
    model->setRuntimeModelId(modelId);
    modelsById[modelId] = model.get();
    additionalModelsById.emplace(modelId, std::move(model));
    return modelId;
}

bool ResourceManager::removeModelByID(uint32_t modelID) {
    if (modelID == 0) {
        return false;
    }

    if ((visModel && visModel->getRuntimeModelId() == modelID) ||
        (heatModel && heatModel->getRuntimeModelId() == modelID) ||
        (commonSubdivision && commonSubdivision->getRuntimeModelId() == modelID)) {
        return false;
    }

    const auto it = additionalModelsById.find(modelID);
    if (it == additionalModelsById.end()) {
        return false;
    }

    if (it->second) {
        it->second->cleanup();
    }
    unregisterModel(it->second);
    additionalModelsById.erase(it);
    return true;
}

std::vector<uint32_t> ResourceManager::getRenderableModelIds() const {
    std::vector<uint32_t> modelIds;
    modelIds.reserve(modelsById.size());
    for (const auto& [modelId, modelPtr] : modelsById) {
        (void)modelPtr;
        if (commonSubdivision && commonSubdivision->getRuntimeModelId() == modelId) {
            continue;
        }
        modelIds.push_back(modelId);
    }

    std::sort(modelIds.begin(), modelIds.end());
    return modelIds;
}

Model* ResourceManager::getModelByID(uint32_t modelID) {
    const auto it = modelsById.find(modelID);
    if (it == modelsById.end()) {
        return nullptr;
    }
    return it->second;
}

const Model* ResourceManager::getModelByID(uint32_t modelID) const {
    const auto it = modelsById.find(modelID);
    if (it == modelsById.end()) {
        return nullptr;
    }
    return it->second;
}

uint32_t ResourceManager::getModelID(Model* model) const {
    return model ? model->getRuntimeModelId() : 0;
}

uint32_t ResourceManager::getVisModelID() const {
    return visModel ? visModel->getRuntimeModelId() : 0;
}

uint32_t ResourceManager::getHeatModelID() const {
    return heatModel ? heatModel->getRuntimeModelId() : 0;
}

uint32_t ResourceManager::getCommonSubdivisionModelID() const {
    return commonSubdivision ? commonSubdivision->getRuntimeModelId() : 0;
}

void ResourceManager::cleanup() {
    clearAdditionalModels();

    if (visModel) {
        visModel->cleanup();
    }
    if (heatModel) {
        heatModel->cleanup();
    }
    if (commonSubdivision) {
        commonSubdivision->cleanup();
    }
}

bool ResourceManager::isReservedModelId(uint32_t modelId) {
    return modelId == 0 || (modelId >= 3 && modelId <= 8);
}

uint32_t ResourceManager::acquireModelId(uint32_t preferredModelId) {
    if (preferredModelId != 0 &&
        preferredModelId <= MaxStencilModelId &&
        !isReservedModelId(preferredModelId) &&
        modelsById.find(preferredModelId) == modelsById.end()) {
        return preferredModelId;
    }

    while (!recycledModelIds.empty()) {
        const uint32_t candidate = recycledModelIds.back();
        recycledModelIds.pop_back();
        if (candidate == 0 || candidate > MaxStencilModelId || isReservedModelId(candidate)) {
            continue;
        }
        if (modelsById.find(candidate) == modelsById.end()) {
            return candidate;
        }
    }

    while (nextModelId <= MaxStencilModelId &&
           (isReservedModelId(nextModelId) ||
            modelsById.find(nextModelId) != modelsById.end())) {
        ++nextModelId;
    }

    if (nextModelId > MaxStencilModelId) {
        std::cerr << "[ResourceManager] Exhausted stencil model IDs" << std::endl;
        return 0;
    }

    return nextModelId++;
}

void ResourceManager::recycleModelId(uint32_t modelId) {
    if (modelId == 0 || modelId > MaxStencilModelId || isReservedModelId(modelId)) {
        return;
    }

    if (std::find(recycledModelIds.begin(), recycledModelIds.end(), modelId) == recycledModelIds.end()) {
        recycledModelIds.push_back(modelId);
    }
}

void ResourceManager::registerModel(std::unique_ptr<Model>& modelSlot, uint32_t preferredModelId) {
    if (!modelSlot) {
        return;
    }

    const uint32_t modelId = acquireModelId(preferredModelId);
    if (modelId == 0) {
        std::cerr << "[ResourceManager] Failed to register model: no available model ID" << std::endl;
        return;
    }
    modelSlot->setRuntimeModelId(modelId);
    modelsById[modelId] = modelSlot.get();
}

void ResourceManager::unregisterModel(std::unique_ptr<Model>& modelSlot) {
    if (!modelSlot) {
        return;
    }

    const uint32_t modelId = modelSlot->getRuntimeModelId();
    if (modelId != 0) {
        modelsById.erase(modelId);
        recycleModelId(modelId);
        modelSlot->setRuntimeModelId(0);
    }
}

glm::vec3 ResourceManager::calculateMaxBoundingBoxSize() const {
    glm::vec3 globalMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 globalMax = glm::vec3(std::numeric_limits<float>::lowest());
    bool hasAnyBounds = false;

    for (uint32_t modelId : getRenderableModelIds()) {
        Model* model = const_cast<Model*>(getModelByID(modelId));
        if (!model || model->getVertexCount() == 0) {
            continue;
        }

        globalMin = glm::min(globalMin, model->getBoundingBoxMin());
        globalMax = glm::max(globalMax, model->getBoundingBoxMax());
        hasAnyBounds = true;
    }

    if (!hasAnyBounds) {
        return glm::vec3(1.0f);
    }

    glm::vec3 size = globalMax - globalMin;

    float greaterLineInterval = 1.0f;
    float snappedWidth = std::ceil(size.x / greaterLineInterval) * greaterLineInterval;
    float snappedDepth = std::ceil(size.z / greaterLineInterval) * greaterLineInterval;
    float snappedHeight = std::ceil(size.y / greaterLineInterval) * greaterLineInterval;

    float minSize = 1.0f;
    snappedWidth = glm::max(snappedWidth, minSize);
    snappedDepth = glm::max(snappedDepth, minSize);
    snappedHeight = glm::max(snappedHeight, minSize);

    float padding = 1.0f;
    if (size.x >= snappedWidth) snappedWidth += padding;
    if (size.z >= snappedDepth) snappedDepth += padding;
    if (size.y >= snappedHeight) snappedHeight += padding;

    return glm::vec3(snappedWidth, snappedDepth, snappedHeight);
}
