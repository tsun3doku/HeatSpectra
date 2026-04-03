#pragma once

#include "NodeGraphCoreTypes.hpp"
#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

class NodePayloadRegistry {
public:
    NodePayloadRegistry() = default;

    template <typename T>
    NodeDataHandle upsert(uint64_t key, T payload) {
        if (key == 0) {
            return {};
        }
        Entry entry{};
        entry.payload = std::make_shared<T>(std::move(payload));
        entry.type = std::type_index(typeid(T));
        entry.revision = revisionCounter.fetch_add(1, std::memory_order_relaxed);
        entry.count = static_cast<uint32_t>(inferCount(*static_cast<T*>(entry.payload.get())));

        entries[key] = entry;
        NodeDataHandle handle{};
        handle.key = key;
        handle.revision = entry.revision;
        handle.count = entry.count;
        return handle;
    }

    template <typename T>
    const T* get(const NodeDataHandle& handle) const {
        auto it = entries.find(handle.key);
        if (it == entries.end()) {
            return nullptr;
        }
        if (it->second.type != std::type_index(typeid(T))) {
            return nullptr;
        }
        return static_cast<const T*>(it->second.payload.get());
    }

    void erase(uint64_t key) {
        entries.erase(key);
    }

    void clear() {
        entries.clear();
    }

    const GeometryData* resolveGeometryHandle(const NodeDataHandle& handle) const {
        if (handle.key == 0) {
            return nullptr;
        }

        return get<GeometryData>(handle);
    }

    bool hasRemeshHandle(const NodeDataHandle& handle) const {
        if (handle.key == 0) {
            return false;
        }

        return get<RemeshData>(handle) != nullptr;
    }

private:
    struct Entry {
        std::shared_ptr<void> payload;
        std::type_index type{typeid(void)};
        uint64_t revision = 0;
        uint32_t count = 0;
    };

    template <typename T>
    static auto inferCount(const T& value) -> decltype(value.size(), uint32_t{}) {
        return static_cast<uint32_t>(value.size());
    }

    static uint32_t inferCount(...) {
        return 0u;
    }

    std::unordered_map<uint64_t, Entry> entries;
    std::atomic<uint64_t> revisionCounter{1};
};
