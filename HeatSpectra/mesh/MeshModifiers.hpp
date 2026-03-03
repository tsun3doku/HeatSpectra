#pragma once

#include "remesher/Remesher.hpp"

#include <cstdint>

class Model;
class ModelSelection;
class ResourceManager;
class UniformBufferManager;
class MemoryAllocator;
class VulkanDevice;

class MeshModifiers {
public:
    MeshModifiers(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager);

    Model* performRemeshing(ModelSelection& modelSelection, int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize, uint32_t targetModelId = 0);
    bool areAllModelsRemeshed() const;

    Remesher& getRemesher() {
        return remesher;
    }
    const Remesher& getRemesher() const {
        return remesher;
    }

    void cleanup();

private:
    ResourceManager& resourceManager;
    Remesher remesher;
};
