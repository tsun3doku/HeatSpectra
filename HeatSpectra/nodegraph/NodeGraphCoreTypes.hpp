#pragma once

#include <cstdint>

struct NodeDataHandle {
    uint64_t key = 0;
    uint64_t revision = 0;
    uint32_t count = 0;

    bool operator==(const NodeDataHandle& other) const {
        return key == other.key &&
               revision == other.revision &&
               count == other.count;
    }

    bool operator<(const NodeDataHandle& other) const {
        if (key != other.key) {
            return key < other.key;
        }
        if (revision != other.revision) {
            return revision < other.revision;
        }
        return count < other.count;
    }
};
