#include "RuntimeModelDisplayTransport.hpp"

#include "nodegraph/NodeModelTransform.hpp"
#include "runtime/ModelDisplayRuntime.hpp"
#include "runtime/RuntimeECS.hpp"

void RuntimeModelDisplayTransport::sync(const ECSRegistry& registry) {
    if (!modelRuntime) {
        return;
    }

    std::unordered_set<uint64_t> nextSocketKeys;

    auto view = registry.view<ModelPackage>();
    for (auto entity : view) {
        uint64_t socketKey = static_cast<uint64_t>(entity);
        if (visibleKeys && visibleKeys->find(socketKey) == visibleKeys->end()) {
            continue;
        }

        const auto& package = registry.get<ModelPackage>(entity);
        nextSocketKeys.insert(socketKey);

        modelRuntime->queueShowSocket(socketKey, NodeModelTransform::toMat4(package.localToWorld));
    }

    for (uint64_t socketKey : activeSocketKeys) {
        if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
            modelRuntime->queueHideSocket(socketKey);
        }
    }

    activeSocketKeys = std::move(nextSocketKeys);
}

void RuntimeModelDisplayTransport::finalizeSync() {
    if (!modelRuntime) {
        return;
    }

    modelRuntime->flush();
}
