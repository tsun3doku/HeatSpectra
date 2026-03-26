#pragma once

#include "remesher/Remesher.hpp"

class VulkanDevice;
class MemoryAllocator;
class ResourceManager;

class MeshModifiers {
public:
    MeshModifiers(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager);

    Remesher& getRemesher() {
        return remesher;
    }

    const Remesher& getRemesher() const {
        return remesher;
    }

    void cleanup() {
    }

private:
    ResourceManager& resourceManager;
    Remesher remesher;
};
