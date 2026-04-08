#pragma once

#include "remesher/Remesher.hpp"

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;

class MeshModifiers {
public:
    MeshModifiers(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager);

    Remesher& getRemesher() {
        return remesher;
    }

    const Remesher& getRemesher() const {
        return remesher;
    }

    void cleanup() {
    }

private:
    ModelRegistry& resourceManager;
    Remesher remesher;
};

