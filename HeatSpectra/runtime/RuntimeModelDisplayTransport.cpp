#include "RuntimeModelDisplayTransport.hpp"

#include "nodegraph/NodeModelTransform.hpp"
#include "runtime/ModelDisplayRuntime.hpp"

void RuntimeModelDisplayTransport::sync(const std::unordered_map<uint64_t, ModelPackage>& packagesBySocket) {
    if (!modelRuntime) {
        return;
    }

    staleSocketKeys.clear();
    std::unordered_set<uint64_t> nextSocketKeys;

    for (const auto& [socketKey, package] : packagesBySocket) {
        nextSocketKeys.insert(socketKey);
        modelRuntime->queueShowSocket(socketKey, NodeModelTransform::toMat4(package.localToWorld));
    }
    staleSocketKeys.reserve(activeSocketKeys.size());
    for (uint64_t socketKey : activeSocketKeys) {
        if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
            staleSocketKeys.push_back(socketKey);
        }
    }

    activeSocketKeys.clear();
    for (uint64_t socketKey : nextSocketKeys) {
        activeSocketKeys.insert(socketKey);
    }
}

void RuntimeModelDisplayTransport::finalizeSync() {
    if (!modelRuntime) {
        return;
    }

    for (uint64_t socketKey : staleSocketKeys) {
        modelRuntime->queueHideSocket(socketKey);
    }
    staleSocketKeys.clear();

    modelRuntime->flush();
}
