#pragma once

#include "runtime/RuntimeProductRegistry.hpp"

class ResourceManager;

class RuntimeModelTransport {
public:
    void setResourceManager(ResourceManager* updatedResourceManager) {
        resourceManager = updatedResourceManager;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void publish(uint64_t socketKey, uint32_t runtimeModelId);
    void remove(uint64_t socketKey);

private:
    ResourceManager* resourceManager = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
};
