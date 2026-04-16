#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include <glm/mat4x4.hpp>

#include "runtime/RuntimeProducts.hpp"

class ModelComputeRuntime;
class ModelRegistry;

class ModelDisplayRuntime {
public:
    explicit ModelDisplayRuntime(ModelRegistry& resourceManager);

    void setComputeRuntime(ModelComputeRuntime* updatedComputeRuntime);

    void queueShowSocket(uint64_t socketKey, const glm::mat4& matrix);
    void queueHideSocket(uint64_t socketKey);
    void flush();

    bool tryGetRuntimeModelId(uint64_t socketKey, uint32_t& outRuntimeModelId) const;

private:
    struct PendingOperation {
        enum class Type {
            ShowSocket,
            HideSocket,
        };

        Type type = Type::ShowSocket;
        uint64_t socketKey = 0;
        glm::mat4 matrix{ 1.0f };
    };

    void showSocketImmediate(uint64_t socketKey, const glm::mat4& matrix);
    void hideSocketImmediate(uint64_t socketKey);

    ModelRegistry& resourceManager;
    ModelComputeRuntime* computeRuntime = nullptr;
    mutable std::mutex executionMutex;
    mutable std::mutex pendingOperationsMutex;
    std::vector<PendingOperation> pendingOperations;
};
