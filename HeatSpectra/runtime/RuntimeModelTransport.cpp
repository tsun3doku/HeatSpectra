#include "RuntimeModelTransport.hpp"

#include "scene/Model.hpp"
#include "vulkan/ResourceManager.hpp"

void RuntimeModelTransport::publish(uint64_t socketKey, uint32_t runtimeModelId) {
    if (!productRegistry || !resourceManager || socketKey == 0 || runtimeModelId == 0) {
        return;
    }

    Model* model = resourceManager->getModelByID(runtimeModelId);
    if (!model) {
        productRegistry->removeModel(socketKey);
        return;
    }

    ModelProduct product{};
    product.runtimeModelId = runtimeModelId;
    product.vertexBuffer = model->getVertexBuffer();
    product.vertexBufferOffset = model->getVertexBufferOffset();
    product.indexBuffer = model->getIndexBuffer();
    product.indexBufferOffset = model->getIndexBufferOffset();
    product.indexCount = static_cast<uint32_t>(model->getIndices().size());
    product.modelMatrix = model->getModelMatrix();
    productRegistry->publishModel(socketKey, product);
}

void RuntimeModelTransport::remove(uint64_t socketKey) {
    if (!productRegistry || socketKey == 0) {
        return;
    }

    productRegistry->removeModel(socketKey);
}
