#pragma once

#include <unordered_map>

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"

class NodeGraphRuntimeBridge {
public:
    void clear() {
        remeshProductByPayloadKey.clear();
    }

    void setRemeshProductForPayload(const NodeDataHandle& payloadHandle, const ProductHandle& productHandle) {
        if (payloadHandle.key == 0 || !productHandle.isValid()) {
            return;
        }

        remeshProductByPayloadKey[payloadHandle.key] = productHandle;
    }

    ProductHandle resolveRemeshProductForPayload(const NodeDataHandle& payloadHandle) const {
        if (payloadHandle.key == 0) {
            return {};
        }

        const auto it = remeshProductByPayloadKey.find(payloadHandle.key);
        if (it == remeshProductByPayloadKey.end()) {
            return {};
        }

        return it->second;
    }

private:
    std::unordered_map<uint64_t, ProductHandle> remeshProductByPayloadKey;
};
