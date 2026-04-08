#include "RuntimeModelTransport.hpp"

#include "runtime/ModelRuntime.hpp"

void RuntimeModelTransport::publish(uint64_t socketKey, uint32_t runtimeModelId) {
    if (!productRegistry || !modelRuntime || socketKey == 0 || runtimeModelId == 0) {
        return;
    }

    ModelProduct product{};
    if (!modelRuntime->exportProduct(runtimeModelId, product)) {
        productRegistry->removeModel(socketKey);
        return;
    }

    productRegistry->publishModel(socketKey, product);
}

void RuntimeModelTransport::remove(uint64_t socketKey) {
    if (!productRegistry || socketKey == 0) {
        return;
    }

    productRegistry->removeModel(socketKey);
}
