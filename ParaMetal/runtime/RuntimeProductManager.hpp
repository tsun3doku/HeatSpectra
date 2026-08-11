#pragma once

#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

class MemoryAllocator;
class VulkanDevice;

class RuntimeProductManager {
public:
    RuntimeProductManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);
    ~RuntimeProductManager();

    template <typename ProductT>
    ProductHandle publish(uint64_t socketKey, ProductT& product) {
        if (socketKey == 0) {
            return {};
        }

        ProductHandle handle = makeHandle<ProductT>(socketKey, product);
        if (!handle.isValid()) {
            return {};
        }

        auto& products = productsFor<ProductT>()[socketKey];
        products.push_back(product);
        product = {};
        return handle;
    }

    template <typename ProductT>
    const ProductT* resolve(const ProductHandle& handle) const {
        if (handle.type != type<ProductT>() || !handle.isValid()) {
            return nullptr;
        }

        auto productsIt = productsFor<ProductT>().find(handle.outputSocketKey);
        if (productsIt == productsFor<ProductT>().end()) {
            return nullptr;
        }

        for (const ProductT& product : productsIt->second) {
            if (hashesMatch(product.hashes, handle.hashes)) {
                return &product;
            }
        }
        return nullptr;
    }

    void destroyAll();

private:
    void destroy(ModelProduct& product);
    void destroy(RemeshProduct& product);
    void destroy(VoronoiProduct& product);
    void destroy(PointProduct& product);
    void destroy(ContactProduct& product);
    void destroy(HeatProduct& product);

    template <typename ProductT>
    static NodeProductType type() {
        if constexpr (std::is_same_v<ProductT, ModelProduct>) return NodeProductType::Model;
        else if constexpr (std::is_same_v<ProductT, RemeshProduct>) return NodeProductType::Remesh;
        else if constexpr (std::is_same_v<ProductT, VoronoiProduct>) return NodeProductType::Voronoi;
        else if constexpr (std::is_same_v<ProductT, PointProduct>) return NodeProductType::Point;
        else if constexpr (std::is_same_v<ProductT, ContactProduct>) return NodeProductType::Contact;
        else if constexpr (std::is_same_v<ProductT, HeatProduct>) return NodeProductType::Heat;
        else return NodeProductType::None;
    }

    template <typename ProductT>
    static const char* productTypeName() {
        if constexpr (std::is_same_v<ProductT, ModelProduct>) return "Model";
        else if constexpr (std::is_same_v<ProductT, RemeshProduct>) return "Remesh";
        else if constexpr (std::is_same_v<ProductT, VoronoiProduct>) return "Voronoi";
        else if constexpr (std::is_same_v<ProductT, PointProduct>) return "Point";
        else if constexpr (std::is_same_v<ProductT, ContactProduct>) return "Contact";
        else if constexpr (std::is_same_v<ProductT, HeatProduct>) return "Heat";
        else return "Unknown";
    }

    template <typename ProductT>
    static ProductHandle makeHandle(uint64_t socketKey, const ProductT& product) {
        ProductHandle handle{};
        handle.type = type<ProductT>();
        handle.outputSocketKey = socketKey;
        handle.hashes = product.hashes;
        return handle;
    }

    static bool hashesMatch(const HashValues& a, const HashValues& b) {
        return a.full == b.full &&
            a.geometry == b.geometry &&
            a.thermal == b.thermal &&
            a.simulation == b.simulation &&
            a.display == b.display;
    }


    template <typename ProductT>
    void destroyAllProducts(std::unordered_map<uint64_t, std::vector<ProductT>>& productsBySocket) {
        for (auto& [socketKey, products] : productsBySocket) {
            (void)socketKey;
            for (auto& product : products) {
                destroy(product);
            }
        }
        productsBySocket.clear();
    }

    template <typename ProductT>
    std::unordered_map<uint64_t, std::vector<ProductT>>& productsFor() {
        if constexpr (std::is_same_v<ProductT, ModelProduct>) return modelProducts;
        else if constexpr (std::is_same_v<ProductT, RemeshProduct>) return remeshProducts;
        else if constexpr (std::is_same_v<ProductT, VoronoiProduct>) return voronoiProducts;
        else if constexpr (std::is_same_v<ProductT, PointProduct>) return pointProducts;
        else if constexpr (std::is_same_v<ProductT, ContactProduct>) return contactProducts;
        else return heatProducts;
    }

    template <typename ProductT>
    const std::unordered_map<uint64_t, std::vector<ProductT>>& productsFor() const {
        if constexpr (std::is_same_v<ProductT, ModelProduct>) return modelProducts;
        else if constexpr (std::is_same_v<ProductT, RemeshProduct>) return remeshProducts;
        else if constexpr (std::is_same_v<ProductT, VoronoiProduct>) return voronoiProducts;
        else if constexpr (std::is_same_v<ProductT, PointProduct>) return pointProducts;
        else if constexpr (std::is_same_v<ProductT, ContactProduct>) return contactProducts;
        else return heatProducts;
    }

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;

    std::unordered_map<uint64_t, std::vector<ModelProduct>> modelProducts;
    std::unordered_map<uint64_t, std::vector<RemeshProduct>> remeshProducts;
    std::unordered_map<uint64_t, std::vector<VoronoiProduct>> voronoiProducts;
    std::unordered_map<uint64_t, std::vector<PointProduct>> pointProducts;
    std::unordered_map<uint64_t, std::vector<ContactProduct>> contactProducts;
    std::unordered_map<uint64_t, std::vector<HeatProduct>> heatProducts;

};
