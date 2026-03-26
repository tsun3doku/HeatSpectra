#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

class ComputeCache {
public:
    struct Handle {
        uint64_t key = 0;
        uint64_t revision = 0;
        uint32_t count = 0;
    };

    ComputeCache() = default;

    template <typename T>
    bool tryGet(uint64_t domainKey, uint64_t inputHash, T& outData, Handle& outHandle) const {
        if (domainKey == 0 || inputHash == 0) {
            return false;
        }
        const auto domainIt = entries.find(domainKey);
        if (domainIt == entries.end()) {
            return false;
        }
        const auto& domainEntries = domainIt->second;
        const auto it = domainEntries.find(inputHash);
        if (it == domainEntries.end()) {
            return false;
        }
        if (it->second.type != std::type_index(typeid(T))) {
            return false;
        }
        outData = *static_cast<const T*>(it->second.payload.get());
        outHandle.key = combine(domainKey, inputHash);
        outHandle.revision = it->second.revision;
        outHandle.count = it->second.count;
        return true;
    }

    template <typename T>
    Handle store(uint64_t domainKey, uint64_t inputHash, T payload) {
        if (domainKey == 0 || inputHash == 0) {
            return {};
        }
        Entry entry{};
        entry.payload = std::make_shared<T>(std::move(payload));
        entry.type = std::type_index(typeid(T));
        entry.revision = revisionCounter.fetch_add(1, std::memory_order_relaxed);
        entry.count = static_cast<uint32_t>(inferCount(*static_cast<T*>(entry.payload.get())));
        entries[domainKey][inputHash] = entry;

        Handle handle{};
        handle.key = combine(domainKey, inputHash);
        handle.revision = entry.revision;
        handle.count = entry.count;
        return handle;
    }

    void erase(uint64_t domainKey, uint64_t inputHash) {
        auto it = entries.find(domainKey);
        if (it == entries.end()) {
            return;
        }
        it->second.erase(inputHash);
    }

    void clear() {
        entries.clear();
    }

    void clearDomain(uint64_t domainKey) {
        entries.erase(domainKey);
    }

    static uint64_t combine(uint64_t seed, uint64_t value);

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

    std::unordered_map<uint64_t, std::unordered_map<uint64_t, Entry>> entries;
    std::atomic<uint64_t> revisionCounter{1};
};
