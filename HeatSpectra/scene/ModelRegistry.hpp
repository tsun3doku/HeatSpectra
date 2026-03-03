#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class SceneController;

class ModelRegistry {
public:
    void setSceneController(SceneController* sceneController);
    void bindNodeModel(uint32_t nodeModelId, uint32_t runtimeModelId);
    void clearNodeBindings();
    bool tryGetNodeModelRuntimeId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const;
    uint32_t getOrLoadModelID(uint32_t nodeModelId, const std::string& modelPath);
    bool removeNodeModel(uint32_t nodeModelId);
    std::size_t removeMissingNodeModels(const std::vector<uint32_t>& liveNodeModelIds);

private:
    SceneController* sceneController = nullptr;
    mutable std::mutex nodeBindingsMutex;
    std::unordered_map<uint32_t, uint32_t> runtimeModelIdByNodeModelId;
    std::unordered_map<uint32_t, std::string> modelPathByNodeModelId;
};
