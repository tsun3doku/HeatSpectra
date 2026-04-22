#pragma once

#include <cstdint>

struct NodeDataHandle {
    uint64_t key = 0;
    uint64_t revision = 0;

    bool operator==(const NodeDataHandle& other) const {
        return key == other.key &&
               revision == other.revision;
    }

    bool operator<(const NodeDataHandle& other) const {
        if (key != other.key) {
            return key < other.key;
        }
        return revision < other.revision;
    }
};
