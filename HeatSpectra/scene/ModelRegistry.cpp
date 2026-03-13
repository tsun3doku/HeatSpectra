#include "ModelRegistry.hpp"

#include "SceneController.hpp"

#include <unordered_set>
#include <vector>

void ModelRegistry::setSceneController(SceneController* sceneControllerHandle) {
    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    sceneController = sceneControllerHandle;
}

void ModelRegistry::bindNodeModel(uint32_t nodeModelId, uint32_t runtimeModelId) {
    if (nodeModelId == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    const auto existingBindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
    if (existingBindingIt != runtimeModelIdByNodeModelId.end() && existingBindingIt->second != 0) {
        nodeModelIdByRuntimeModelId.erase(existingBindingIt->second);
    }
    if (runtimeModelId == 0) {
        runtimeModelIdByNodeModelId.erase(nodeModelId);
        modelPathByNodeModelId.erase(nodeModelId);
        return;
    }

    runtimeModelIdByNodeModelId[nodeModelId] = runtimeModelId;
    nodeModelIdByRuntimeModelId[runtimeModelId] = nodeModelId;
}

void ModelRegistry::clearNodeBindings() {
    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    runtimeModelIdByNodeModelId.clear();
    nodeModelIdByRuntimeModelId.clear();
    modelPathByNodeModelId.clear();
}

bool ModelRegistry::tryGetNodeModelRuntimeId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const {
    outRuntimeModelId = 0;
    if (nodeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
    if (bindingIt == runtimeModelIdByNodeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outRuntimeModelId = bindingIt->second;
    return true;
}

bool ModelRegistry::tryGetRuntimeModelNodeId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const {
    outNodeModelId = 0;
    if (runtimeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    const auto bindingIt = nodeModelIdByRuntimeModelId.find(runtimeModelId);
    if (bindingIt == nodeModelIdByRuntimeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outNodeModelId = bindingIt->second;
    return true;
}

uint32_t ModelRegistry::getOrLoadModelID(uint32_t nodeModelId, const std::string& modelPath) {
    SceneController* activeSceneController = nullptr;
    uint32_t existingRuntimeModelId = 0;
    std::string existingModelPath;

    {
        std::lock_guard<std::mutex> lock(nodeBindingsMutex);
        activeSceneController = sceneController;
        if (nodeModelId != 0) {
            const auto runtimeIt = runtimeModelIdByNodeModelId.find(nodeModelId);
            if (runtimeIt != runtimeModelIdByNodeModelId.end()) {
                existingRuntimeModelId = runtimeIt->second;
            }

            const auto pathIt = modelPathByNodeModelId.find(nodeModelId);
            if (pathIt != modelPathByNodeModelId.end()) {
                existingModelPath = pathIt->second;
            }
        }
    }

    if (!activeSceneController) {
        return 0;
    }

    if (existingRuntimeModelId != 0 && !existingModelPath.empty() && existingModelPath == modelPath) {
        return existingRuntimeModelId;
    }

    const uint32_t runtimeModelId = activeSceneController->loadModel(modelPath, existingRuntimeModelId);
    if (nodeModelId != 0) {
        std::lock_guard<std::mutex> lock(nodeBindingsMutex);
        if (existingRuntimeModelId != 0 && existingRuntimeModelId != runtimeModelId) {
            nodeModelIdByRuntimeModelId.erase(existingRuntimeModelId);
        }
        if (runtimeModelId != 0) {
            runtimeModelIdByNodeModelId[nodeModelId] = runtimeModelId;
            nodeModelIdByRuntimeModelId[runtimeModelId] = nodeModelId;
            modelPathByNodeModelId[nodeModelId] = modelPath;
        } else {
            runtimeModelIdByNodeModelId.erase(nodeModelId);
            if (existingRuntimeModelId != 0) {
                nodeModelIdByRuntimeModelId.erase(existingRuntimeModelId);
            }
            modelPathByNodeModelId.erase(nodeModelId);
        }
    }

    return runtimeModelId;
}

bool ModelRegistry::removeNodeModel(uint32_t nodeModelId) {
    if (nodeModelId == 0) {
        return false;
    }

    SceneController* activeSceneController = nullptr;
    uint32_t runtimeModelId = 0;
    bool hasMappedModel = false;
    {
        std::lock_guard<std::mutex> lock(nodeBindingsMutex);
        activeSceneController = sceneController;
        const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
        hasMappedModel = bindingIt != runtimeModelIdByNodeModelId.end() && bindingIt->second != 0;
        if (hasMappedModel) {
            runtimeModelId = bindingIt->second;
        }
        runtimeModelIdByNodeModelId.erase(nodeModelId);
        if (runtimeModelId != 0) {
            nodeModelIdByRuntimeModelId.erase(runtimeModelId);
        }
        modelPathByNodeModelId.erase(nodeModelId);
    }

    if (!activeSceneController || !hasMappedModel || runtimeModelId == 0) {
        return false;
    }

    return activeSceneController->removeModelByID(runtimeModelId);
}

std::size_t ModelRegistry::removeMissingNodeModels(const std::vector<uint32_t>& liveNodeModelIds) {
    std::unordered_set<uint32_t> liveNodeModelIdSet;
    liveNodeModelIdSet.reserve(liveNodeModelIds.size());
    for (uint32_t nodeModelId : liveNodeModelIds) {
        if (nodeModelId != 0) {
            liveNodeModelIdSet.insert(nodeModelId);
        }
    }

    SceneController* activeSceneController = nullptr;
    std::vector<uint32_t> staleRuntimeModelIds;
    std::unordered_set<uint32_t> runtimeModelIdsStillMapped;
    {
        std::lock_guard<std::mutex> lock(nodeBindingsMutex);
        activeSceneController = sceneController;
        if (runtimeModelIdByNodeModelId.empty()) {
            return 0;
        }

        staleRuntimeModelIds.reserve(runtimeModelIdByNodeModelId.size());
        for (auto bindingIt = runtimeModelIdByNodeModelId.begin();
             bindingIt != runtimeModelIdByNodeModelId.end();) {
            const uint32_t nodeModelId = bindingIt->first;
            const uint32_t runtimeModelId = bindingIt->second;
            const bool nodeStillExists = liveNodeModelIdSet.find(nodeModelId) != liveNodeModelIdSet.end();
            if (nodeStillExists) {
                if (runtimeModelId != 0) {
                    runtimeModelIdsStillMapped.insert(runtimeModelId);
                }
                ++bindingIt;
                continue;
            }

            if (runtimeModelId != 0) {
                staleRuntimeModelIds.push_back(runtimeModelId);
                nodeModelIdByRuntimeModelId.erase(runtimeModelId);
            }
            modelPathByNodeModelId.erase(nodeModelId);
            bindingIt = runtimeModelIdByNodeModelId.erase(bindingIt);
        }
    }

    if (!activeSceneController || staleRuntimeModelIds.empty()) {
        return 0;
    }

    std::unordered_set<uint32_t> removedRuntimeModelIds;
    std::size_t removedModelCount = 0;
    for (uint32_t runtimeModelId : staleRuntimeModelIds) {
        if (runtimeModelId == 0) {
            continue;
        }
        if (runtimeModelIdsStillMapped.find(runtimeModelId) != runtimeModelIdsStillMapped.end()) {
            continue;
        }
        if (!removedRuntimeModelIds.insert(runtimeModelId).second) {
            continue;
        }
        if (activeSceneController->removeModelByID(runtimeModelId)) {
            ++removedModelCount;
        }
    }

    return removedModelCount;
}
