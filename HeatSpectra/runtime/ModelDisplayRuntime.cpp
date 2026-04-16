#include "ModelDisplayRuntime.hpp"

#include "runtime/ModelComputeRuntime.hpp"
#include "vulkan/ModelRegistry.hpp"

ModelDisplayRuntime::ModelDisplayRuntime(ModelRegistry& resourceManager)
    : resourceManager(resourceManager) {
}

void ModelDisplayRuntime::setComputeRuntime(ModelComputeRuntime* updatedComputeRuntime) {
    std::lock_guard<std::mutex> lock(executionMutex);
    computeRuntime = updatedComputeRuntime;
}

void ModelDisplayRuntime::queueShowSocket(uint64_t socketKey, const glm::mat4& matrix) {
    if (socketKey == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(pendingOperationsMutex);
    pendingOperations.push_back(PendingOperation{
        PendingOperation::Type::ShowSocket,
        socketKey,
        matrix });
}

void ModelDisplayRuntime::queueHideSocket(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(pendingOperationsMutex);
    pendingOperations.push_back(PendingOperation{
        PendingOperation::Type::HideSocket,
        socketKey,
        glm::mat4(1.0f) });
}

void ModelDisplayRuntime::flush() {
    std::vector<PendingOperation> operations;
    {
        std::lock_guard<std::mutex> pendingLock(pendingOperationsMutex);
        operations.swap(pendingOperations);
    }

    if (operations.empty()) {
        return;
    }

    std::lock_guard<std::mutex> executionLock(executionMutex);
    for (const PendingOperation& operation : operations) {
        switch (operation.type) {
        case PendingOperation::Type::ShowSocket:
            showSocketImmediate(operation.socketKey, operation.matrix);
            break;
        case PendingOperation::Type::HideSocket:
            hideSocketImmediate(operation.socketKey);
            break;
        }
    }
}

bool ModelDisplayRuntime::tryGetRuntimeModelId(uint64_t socketKey, uint32_t& outRuntimeModelId) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    if (!computeRuntime) {
        outRuntimeModelId = 0;
        return false;
    }

    return computeRuntime->tryGetRuntimeModelId(socketKey, outRuntimeModelId);
}

void ModelDisplayRuntime::showSocketImmediate(uint64_t socketKey, const glm::mat4& matrix) {
    if (!computeRuntime) {
        return;
    }

    uint32_t runtimeModelId = 0;
    if (!computeRuntime->tryGetRuntimeModelId(socketKey, runtimeModelId) || runtimeModelId == 0) {
        return;
    }

    resourceManager.setModelMatrix(runtimeModelId, matrix);
    resourceManager.setModelVisible(runtimeModelId, true);
}

void ModelDisplayRuntime::hideSocketImmediate(uint64_t socketKey) {
    if (!computeRuntime) {
        return;
    }

    uint32_t runtimeModelId = 0;
    if (!computeRuntime->tryGetRuntimeModelId(socketKey, runtimeModelId) || runtimeModelId == 0) {
        return;
    }

    resourceManager.setModelVisible(runtimeModelId, false);
}
