#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

#include "MemoryAllocator.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "scene/Model.hpp"
#include "ModelRegistry.hpp"

ModelRegistry::ModelRegistry(MemoryAllocator& memoryAllocator)
    : memoryAllocator(memoryAllocator) {
}

ModelRegistry::~ModelRegistry() {
}

void ModelRegistry::clearAdditionalModels() {
    for (auto& [modelId, model] : additionalModelsById) {
        (void)modelId;
        if (model) {
            model->cleanup();
        }
        unregisterModel(model);
    }
    additionalModelsById.clear();
}

void ModelRegistry::setModels(std::unique_ptr<Model> newVisModel, std::unique_ptr<Model> newCommonSubdivision, std::unique_ptr<Model> newHeatModel) {
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

    registerModel(visModel, visModelId);
    registerModel(heatModel, heatModelId);
    registerModel(commonSubdivision, commonSubdivisionId);
}

uint32_t ModelRegistry::addModel(std::unique_ptr<Model> model, uint32_t preferredModelId) {
    if (!model) {
        return 0;
    }

    const uint32_t modelId = acquireModelId(preferredModelId);
    model->setRuntimeModelId(modelId);
    modelsById[modelId] = model.get();
    additionalModelsById.emplace(modelId, std::move(model));
    return modelId;
}

bool ModelRegistry::removeModelByID(uint32_t modelID) {
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

std::vector<uint32_t> ModelRegistry::getRenderableModelIds() const {
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

bool ModelRegistry::hasModel(uint32_t modelID) const {
    return modelsById.find(modelID) != modelsById.end();
}

bool ModelRegistry::exportProduct(uint32_t modelID, ModelProduct& outProduct) const {
    outProduct = {};

    Model* model = const_cast<Model*>(findModel(modelID));
    if (!model) {
        return false;
    }

    outProduct.runtimeModelId = modelID;
    outProduct.vertexBuffer = model->getVertexBuffer();
    outProduct.vertexBufferOffset = model->getVertexBufferOffset();
    outProduct.indexBuffer = model->getIndexBuffer();
    outProduct.indexBufferOffset = model->getIndexBufferOffset();
    outProduct.indexCount = static_cast<uint32_t>(model->getIndices().size());
    outProduct.renderVertexBuffer = model->getRenderVertexBuffer();
    outProduct.renderVertexBufferOffset = model->getRenderVertexBufferOffset();
    outProduct.renderIndexBuffer = model->getRenderIndexBuffer();
    outProduct.renderIndexBufferOffset = model->getRenderIndexBufferOffset();
    outProduct.renderIndexCount = static_cast<uint32_t>(model->getRenderIndices().size());
    outProduct.modelMatrix = model->getModelMatrix();
    outProduct.contentHash = computeContentHash(outProduct);
    return outProduct.isValid();
}

bool ModelRegistry::setModelMatrix(uint32_t modelID, const glm::mat4& matrix) {
    Model* model = findModel(modelID);
    if (!model) {
        return false;
    }

    model->setModelMatrix(matrix);
    return true;
}

bool ModelRegistry::tryGetModelMatrix(uint32_t modelID, glm::mat4& outMatrix) const {
    Model* model = const_cast<Model*>(findModel(modelID));
    if (!model) {
        return false;
    }

    outMatrix = model->getModelMatrix();
    return true;
}

bool ModelRegistry::tryGetBoundingBoxCenter(uint32_t modelID, glm::vec3& outCenter) const {
    Model* model = const_cast<Model*>(findModel(modelID));
    if (!model) {
        return false;
    }

    outCenter = model->getBoundingBoxCenter();
    return true;
}

bool ModelRegistry::tryGetBoundingBoxMinMax(uint32_t modelID, glm::vec3& outMin, glm::vec3& outMax) const {
    Model* model = const_cast<Model*>(findModel(modelID));
    if (!model) {
        return false;
    }

    outMin = model->getBoundingBoxMin();
    outMax = model->getBoundingBoxMax();
    return true;
}

bool ModelRegistry::tryGetWorldBoundingBoxCenter(uint32_t modelID, glm::vec3& outCenter) const {
    glm::vec3 localCenter(0.0f);
    glm::mat4 modelMatrix(1.0f);
    if (!tryGetBoundingBoxCenter(modelID, localCenter) || !tryGetModelMatrix(modelID, modelMatrix)) {
        return false;
    }

    outCenter = glm::vec3(modelMatrix * glm::vec4(localCenter, 1.0f));
    return true;
}

Model* ModelRegistry::findModel(uint32_t modelID) {
    const auto it = modelsById.find(modelID);
    if (it == modelsById.end()) {
        return nullptr;
    }
    return it->second;
}

const Model* ModelRegistry::findModel(uint32_t modelID) const {
    const auto it = modelsById.find(modelID);
    if (it == modelsById.end()) {
        return nullptr;
    }
    return it->second;
}

uint32_t ModelRegistry::getVisModelID() const {
    return visModel ? visModel->getRuntimeModelId() : 0;
}

uint32_t ModelRegistry::getHeatModelID() const {
    return heatModel ? heatModel->getRuntimeModelId() : 0;
}

uint32_t ModelRegistry::getCommonSubdivisionModelID() const {
    return commonSubdivision ? commonSubdivision->getRuntimeModelId() : 0;
}

void ModelRegistry::cleanup() {
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

bool ModelRegistry::isReservedModelId(uint32_t modelId) {
    return modelId == 0 || (modelId >= 3 && modelId <= 8);
}

uint32_t ModelRegistry::acquireModelId(uint32_t preferredModelId) {
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
        std::cerr << "[ModelRegistry] Exhausted stencil model IDs" << std::endl;
        return 0;
    }

    return nextModelId++;
}

void ModelRegistry::recycleModelId(uint32_t modelId) {
    if (modelId == 0 || modelId > MaxStencilModelId || isReservedModelId(modelId)) {
        return;
    }

    if (std::find(recycledModelIds.begin(), recycledModelIds.end(), modelId) == recycledModelIds.end()) {
        recycledModelIds.push_back(modelId);
    }
}

void ModelRegistry::registerModel(std::unique_ptr<Model>& modelSlot, uint32_t preferredModelId) {
    if (!modelSlot) {
        return;
    }

    const uint32_t modelId = acquireModelId(preferredModelId);
    if (modelId == 0) {
        std::cerr << "[ModelRegistry] Failed to register model: no available model ID" << std::endl;
        return;
    }
    modelSlot->setRuntimeModelId(modelId);
    modelsById[modelId] = modelSlot.get();
}

void ModelRegistry::unregisterModel(std::unique_ptr<Model>& modelSlot) {
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

glm::vec3 ModelRegistry::calculateMaxBoundingBoxSize() const {
    glm::vec3 globalMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 globalMax = glm::vec3(std::numeric_limits<float>::lowest());
    bool hasAnyBounds = false;

    for (uint32_t modelId : getRenderableModelIds()) {
        Model* model = const_cast<Model*>(findModel(modelId));
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

