#pragma once

#include "NodeGraphTypes.hpp"

#include <cstddef>
#include <unordered_map>

#include <QPixmap>
#include <QString>

struct IconCacheKey {
    NodeTypeId typeId{};
    int iconWidthKey = 0;

    bool operator==(const IconCacheKey& other) const noexcept;
};

struct IconCacheKeyHash {
    std::size_t operator()(const IconCacheKey& key) const noexcept;
};

class NodeGraphIconRegistry {
public:
    static QPixmap iconForType(const NodeTypeId& typeId, qreal targetPixelWidth);

private:
    static QString iconPathForType(const NodeTypeId& typeId, qreal targetPixelWidth);
    static int iconWidthCacheKey(qreal targetPixelWidth);
    static std::unordered_map<IconCacheKey, QPixmap, IconCacheKeyHash> iconCache;
};
