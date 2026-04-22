#pragma once

#include "NodeGraphCoreTypes.hpp"
#include "NodeGraphTypes.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

struct GeometryData;

class NodePayloadRegistry {
public:
    NodePayloadRegistry() = default;

    template <typename T>
    NodeDataHandle store(uint64_t key, T payload) {
        if (key == 0) {
            return {};
        }
        payload.sealPayload();
        Entry entry{};
        entry.payload = std::make_shared<T>(std::move(payload));
        entry.type = std::type_index(typeid(T));
        entry.revision = revisionCounter.fetch_add(1, std::memory_order_relaxed);
        entries[key] = entry;
        NodeDataHandle handle{};
        handle.key = key;
        handle.revision = entry.revision;
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

    void erase(uint64_t key);
    void clear();
    const GeometryData* resolveGeometryHandle(const NodeDataHandle& handle) const;
    bool hasRemeshHandle(const NodeDataHandle& handle) const;

    const GeometryData* resolveGeometry(NodePayloadType type, const NodeDataHandle& handle) const;
    uint64_t resolvePayloadHash(NodePayloadType type, const NodeDataHandle& handle) const;

private:
    struct Entry {
        std::shared_ptr<void> payload;
        std::type_index type{typeid(void)};
        uint64_t revision = 0;
    };

    std::unordered_map<uint64_t, Entry> entries;
    std::atomic<uint64_t> revisionCounter{1};
};
