#pragma once

#include "runtime/RuntimeProductRegistry.hpp"

class ModelRuntime;

class RuntimeModelTransport {
public:
    void setRuntime(ModelRuntime* updatedRuntime) {
        modelRuntime = updatedRuntime;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void publish(uint64_t socketKey, uint32_t runtimeModelId);
    void remove(uint64_t socketKey);

private:
    ModelRuntime* modelRuntime = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
};
