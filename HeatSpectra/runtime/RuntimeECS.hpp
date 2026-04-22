#pragma once

#include <entt/entt.hpp>
#include <cstdint>
#include <functional>
#include <unordered_set>

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

enum class ECSEntity : uint64_t {};
using ECSRegistry = entt::basic_registry<ECSEntity>;

template <>
struct std::hash<ECSEntity> {
    size_t operator()(ECSEntity e) const noexcept {
        return std::hash<uint64_t>{}(static_cast<uint64_t>(e));
    }
};

template <typename ProductT>
NodeProductType productTypeFor();

template <>
inline NodeProductType productTypeFor<ModelProduct>() {
    return NodeProductType::Model;
}

template <>
inline NodeProductType productTypeFor<RemeshProduct>() {
    return NodeProductType::Remesh;
}

template <>
inline NodeProductType productTypeFor<VoronoiProduct>() {
    return NodeProductType::Voronoi;
}

template <>
inline NodeProductType productTypeFor<ContactProduct>() {
    return NodeProductType::Contact;
}

template <>
inline NodeProductType productTypeFor<HeatProduct>() {
    return NodeProductType::Heat;
}

template <typename ProductT>
const ProductT* tryGetProduct(const ECSRegistry& registry, uint64_t socketKey) {
    auto entity = static_cast<ECSEntity>(socketKey);
    if (registry.valid(entity) && registry.all_of<ProductT>(entity)) {
        return &registry.get<ProductT>(entity);
    }
    return nullptr;
}

template <typename ProductT>
ProductHandle getPublishedHandle(const ECSRegistry& registry, uint64_t outputSocketKey) {
    ProductHandle handle{};
    handle.type = productTypeFor<ProductT>();
    handle.outputSocketKey = outputSocketKey;

    if (outputSocketKey == 0) {
        return {};
    }

    const auto entity = static_cast<ECSEntity>(outputSocketKey);
    if (!registry.valid(entity) || !registry.all_of<ProductT>(entity)) {
        return {};
    }

    return handle;
}

inline std::unordered_set<ECSEntity> collectPackageEntities(const ECSRegistry& registry) {
    std::unordered_set<ECSEntity> entities;
    for (auto entity : registry.view<ModelPackage>()) entities.insert(entity);
    for (auto entity : registry.view<RemeshPackage>()) entities.insert(entity);
    for (auto entity : registry.view<VoronoiPackage>()) entities.insert(entity);
    for (auto entity : registry.view<ContactPackage>()) entities.insert(entity);
    for (auto entity : registry.view<HeatPackage>()) entities.insert(entity);
    return entities;
}

inline void destroyStaleEntities(ECSRegistry& registry, std::unordered_set<ECSEntity>& staleEntities) {
    for (ECSEntity entity : staleEntities) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
}
