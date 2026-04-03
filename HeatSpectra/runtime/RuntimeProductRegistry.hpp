#pragma once

#include <map>
#include <unordered_map>
#include <utility>

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimeProducts.hpp"

class RuntimeProductRegistry {
public:
    void publishModel(uint64_t outputSocketKey, const ModelProduct& product) {
        if (outputSocketKey == 0 || !product.isValid()) {
            removeModel(outputSocketKey);
            return;
        }

        modelBySocket[outputSocketKey] = product;
        bumpRevision(NodeProductType::Model, outputSocketKey);
    }

    void removeModel(uint64_t outputSocketKey) {
        if (outputSocketKey == 0) {
            return;
        }

        modelBySocket.erase(outputSocketKey);
        bumpRevision(NodeProductType::Model, outputSocketKey);
    }

    const ModelProduct* resolveModel(const ProductHandle& handle) const {
        if (handle.type != NodeProductType::Model || !handle.isValid()) {
            return nullptr;
        }

        const auto it = modelBySocket.find(handle.outputSocketKey);
        if (it == modelBySocket.end()) {
            return nullptr;
        }

        return &it->second;
    }

    void publishRemesh(uint64_t outputSocketKey, const RemeshProduct& product) {
        if (outputSocketKey == 0 || !product.isValid()) {
            removeRemesh(outputSocketKey);
            return;
        }

        remeshBySocket[outputSocketKey] = product;
        bumpRevision(NodeProductType::Remesh, outputSocketKey);
    }

    void removeRemesh(uint64_t outputSocketKey) {
        if (outputSocketKey == 0) {
            return;
        }

        remeshBySocket.erase(outputSocketKey);
        bumpRevision(NodeProductType::Remesh, outputSocketKey);
    }

    const RemeshProduct* resolveRemesh(const ProductHandle& handle) const {
        if (handle.type != NodeProductType::Remesh || !handle.isValid()) {
            return nullptr;
        }

        const auto it = remeshBySocket.find(handle.outputSocketKey);
        if (it == remeshBySocket.end()) {
            return nullptr;
        }

        return &it->second;
    }

    void publishVoronoi(uint64_t outputSocketKey, const VoronoiProduct& product) {
        if (outputSocketKey == 0 || !product.isValid()) {
            removeVoronoi(outputSocketKey);
            return;
        }

        voronoiBySocket[outputSocketKey] = product;
        bumpRevision(NodeProductType::Voronoi, outputSocketKey);
    }

    void removeVoronoi(uint64_t outputSocketKey) {
        if (outputSocketKey == 0) {
            return;
        }

        voronoiBySocket.erase(outputSocketKey);
        bumpRevision(NodeProductType::Voronoi, outputSocketKey);
    }

    const VoronoiProduct* resolve(const ProductHandle& handle) const {
        if (handle.type != NodeProductType::Voronoi || !handle.isValid()) {
            return nullptr;
        }

        const auto it = voronoiBySocket.find(handle.outputSocketKey);
        if (it == voronoiBySocket.end()) {
            return nullptr;
        }

        return &it->second;
    }

    void publishContact(uint64_t outputSocketKey, const ContactProduct& product) {
        if (outputSocketKey == 0 || !product.isValid()) {
            removeContact(outputSocketKey);
            return;
        }

        contactBySocket[outputSocketKey] = product;
        bumpRevision(NodeProductType::Contact, outputSocketKey);
    }

    void removeContact(uint64_t outputSocketKey) {
        if (outputSocketKey == 0) {
            return;
        }

        contactBySocket.erase(outputSocketKey);
        bumpRevision(NodeProductType::Contact, outputSocketKey);
    }

    const ContactProduct* resolveContact(const ProductHandle& handle) const {
        if (handle.type != NodeProductType::Contact || !handle.isValid()) {
            return nullptr;
        }

        const auto it = contactBySocket.find(handle.outputSocketKey);
        if (it == contactBySocket.end()) {
            return nullptr;
        }
        return &it->second;
    }

    ProductHandle getPublishedHandle(NodeProductType type, uint64_t outputSocketKey) const {
        if (outputSocketKey == 0 || !containsProduct(type, outputSocketKey)) {
            return {};
        }

        ProductHandle handle{};
        handle.type = type;
        handle.outputSocketKey = outputSocketKey;
        handle.outputRevision = getRevision(type, outputSocketKey);
        return handle;
    }

private:
    using PublicationKey = std::pair<NodeProductType, uint64_t>;

    static PublicationKey publicationKey(NodeProductType type, uint64_t outputSocketKey) {
        return PublicationKey{type, outputSocketKey};
    }

    uint64_t bumpRevision(NodeProductType type, uint64_t outputSocketKey) {
        if (outputSocketKey == 0 || type == NodeProductType::None) {
            return 0;
        }

        return ++revisionByProduct[publicationKey(type, outputSocketKey)];
    }

    uint64_t getRevision(NodeProductType type, uint64_t outputSocketKey) const {
        const auto it = revisionByProduct.find(publicationKey(type, outputSocketKey));
        if (it == revisionByProduct.end()) {
            return 0;
        }

        return it->second;
    }

    bool containsProduct(NodeProductType type, uint64_t outputSocketKey) const {
        switch (type) {
        case NodeProductType::Model:
            return modelBySocket.find(outputSocketKey) != modelBySocket.end();
        case NodeProductType::Remesh:
            return remeshBySocket.find(outputSocketKey) != remeshBySocket.end();
        case NodeProductType::Voronoi:
            return voronoiBySocket.find(outputSocketKey) != voronoiBySocket.end();
        case NodeProductType::Contact:
            return contactBySocket.find(outputSocketKey) != contactBySocket.end();
        default:
            return false;
        }
    }

    std::unordered_map<uint64_t, ModelProduct> modelBySocket;
    std::unordered_map<uint64_t, RemeshProduct> remeshBySocket;
    std::unordered_map<uint64_t, VoronoiProduct> voronoiBySocket;
    std::unordered_map<uint64_t, ContactProduct> contactBySocket;
    std::map<PublicationKey, uint64_t> revisionByProduct;
};
