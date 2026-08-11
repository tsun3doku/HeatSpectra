#pragma once

#include "runtime/package/RuntimePackages.hpp"

#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

class RuntimePackageManager {
public:
    void beginCompile() {
        staleSockets.clear();
        retainedSockets.clear();
        collectKeys(modelPackages);
        collectKeys(pointPackages);
        collectKeys(remeshPackages);
        collectKeys(voronoiPackages);
        collectKeys(contactPackages);
        collectKeys(heatPackages);
    }

    template <typename PackageT>
    void apply(uint64_t socketKey, const PackageT& package) {
        if (socketKey == 0 || package.hashes.full == 0) {
            return;
        }

        auto& packages = packagesFor<PackageT>();
        const auto it = packages.find(socketKey);

        const bool computeChanged =
            it == packages.end() ||
            !package.hasValidProduct() ||
            it->second.computeHash() != package.computeHash();

        packages.insert_or_assign(socketKey, package);
        staleSockets.erase(socketKey);

        if (computeChanged) {
            retainedSockets.erase(socketKey);
        } else {
            retainedSockets.insert(socketKey);
        }
    }

    void retain(uint64_t socketKey) {
        if (socketKey == 0 || !hasPackage(socketKey)) {
            return;
        }
        staleSockets.erase(socketKey);
        retainedSockets.insert(socketKey);
    }

    void setProductHandle(uint64_t socketKey, const ProductHandle& handle) {
        setProductHandle(modelPackages, socketKey, handle);
        setProductHandle(pointPackages, socketKey, handle);
        setProductHandle(remeshPackages, socketKey, handle);
        setProductHandle(voronoiPackages, socketKey, handle);
        setProductHandle(contactPackages, socketKey, handle);
        setProductHandle(heatPackages, socketKey, handle);
    }

    template <typename PackageT>
    const PackageT* find(uint64_t socketKey) const {
        if (staleSockets.find(socketKey) != staleSockets.end()) {
            return nullptr;
        }
        if (retainedSockets.find(socketKey) != retainedSockets.end()) {
            return nullptr;
        }
        const auto& packages = packagesFor<PackageT>();
        auto it = packages.find(socketKey);
        return it != packages.end() ? &it->second : nullptr;
    }

    template <typename PackageT>
    const PackageT* findAny(uint64_t socketKey) const {
        if (staleSockets.find(socketKey) != staleSockets.end()) {
            return nullptr;
        }
        const auto& packages = packagesFor<PackageT>();
        auto it = packages.find(socketKey);
        return it != packages.end() ? &it->second : nullptr;
    }

    template <typename PackageT>
    const PackageT* findStored(uint64_t socketKey) const {
        const auto& packages = packagesFor<PackageT>();
        const auto it = packages.find(socketKey);
        return it != packages.end() ? &it->second : nullptr;
    }

    const std::unordered_map<uint64_t, ModelPackage>& models() const { return modelPackages; }
    const std::unordered_map<uint64_t, PointPackage>& points() const { return pointPackages; }
    const std::unordered_map<uint64_t, RemeshPackage>& remeshes() const { return remeshPackages; }
    const std::unordered_map<uint64_t, VoronoiPackage>& voronois() const { return voronoiPackages; }
    const std::unordered_map<uint64_t, ContactPackage>& contacts() const { return contactPackages; }
    const std::unordered_map<uint64_t, HeatPackage>& heatSystems() const { return heatPackages; }
    const std::unordered_set<uint64_t>& staleSocketKeys() const { return staleSockets; }

    bool isCurrent(uint64_t socketKey) const {
        return staleSockets.find(socketKey) == staleSockets.end();
    }

    void destroyStale() {
        eraseStale(modelPackages);
        eraseStale(pointPackages);
        eraseStale(remeshPackages);
        eraseStale(voronoiPackages);
        eraseStale(contactPackages);
        eraseStale(heatPackages);
        staleSockets.clear();
    }

private:
    template <typename PackageT>
    std::unordered_map<uint64_t, PackageT>& packagesFor() {
        if constexpr (std::is_same_v<PackageT, ModelPackage>) return modelPackages;
        else if constexpr (std::is_same_v<PackageT, PointPackage>) return pointPackages;
        else if constexpr (std::is_same_v<PackageT, RemeshPackage>) return remeshPackages;
        else if constexpr (std::is_same_v<PackageT, VoronoiPackage>) return voronoiPackages;
        else if constexpr (std::is_same_v<PackageT, ContactPackage>) return contactPackages;
        else return heatPackages;
    }

    template <typename PackageT>
    const std::unordered_map<uint64_t, PackageT>& packagesFor() const {
        if constexpr (std::is_same_v<PackageT, ModelPackage>) return modelPackages;
        else if constexpr (std::is_same_v<PackageT, PointPackage>) return pointPackages;
        else if constexpr (std::is_same_v<PackageT, RemeshPackage>) return remeshPackages;
        else if constexpr (std::is_same_v<PackageT, VoronoiPackage>) return voronoiPackages;
        else if constexpr (std::is_same_v<PackageT, ContactPackage>) return contactPackages;
        else return heatPackages;
    }

    template <typename PackageT>
    void collectKeys(const std::unordered_map<uint64_t, PackageT>& packages) {
        for (const auto& [socketKey, package] : packages) {
            (void)package;
            staleSockets.insert(socketKey);
        }
    }

    template <typename PackageT>
    void eraseStale(std::unordered_map<uint64_t, PackageT>& packages) {
        for (auto it = packages.begin(); it != packages.end();) {
            if (staleSockets.find(it->first) != staleSockets.end()) {
                it = packages.erase(it);
            } else {
                ++it;
            }
        }
    }

    template <typename PackageT>
    static void setProductHandle(std::unordered_map<uint64_t, PackageT>& packages, uint64_t socketKey, const ProductHandle& handle) {
        auto it = packages.find(socketKey);
        if (it != packages.end()) {
            it->second.productHandle = handle;
        }
    }

    bool hasPackage(uint64_t socketKey) const {
        return modelPackages.find(socketKey) != modelPackages.end() ||
            pointPackages.find(socketKey) != pointPackages.end() ||
            remeshPackages.find(socketKey) != remeshPackages.end() ||
            voronoiPackages.find(socketKey) != voronoiPackages.end() ||
            contactPackages.find(socketKey) != contactPackages.end() ||
            heatPackages.find(socketKey) != heatPackages.end();
    }

    std::unordered_map<uint64_t, ModelPackage> modelPackages;
    std::unordered_map<uint64_t, PointPackage> pointPackages;
    std::unordered_map<uint64_t, RemeshPackage> remeshPackages;
    std::unordered_map<uint64_t, VoronoiPackage> voronoiPackages;
    std::unordered_map<uint64_t, ContactPackage> contactPackages;
    std::unordered_map<uint64_t, HeatPackage> heatPackages;
    std::unordered_set<uint64_t> staleSockets;
    std::unordered_set<uint64_t> retainedSockets;
};
