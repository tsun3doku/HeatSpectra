#include "NodeGraphIconRegistry.hpp"

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

std::unordered_map<IconCacheKey, QPixmap, IconCacheKeyHash> NodeGraphIconRegistry::iconCache{};

static const char* iconFolderForType(const NodeTypeId& typeId) {
    if (typeId == "contact") return "Contact";
    if (typeId == "heat_solve") return "HeatSystem";
    if (typeId == "model") return "Model";
    if (typeId == "heat_receiver") return "HeatReceiver";
    if (typeId == "heat_source") return "HeatSource";
    if (typeId == "remesh") return "Remesh";
    if (typeId == "transform") return "Transform";
    if (typeId == "voronoi") return "VoronoiSystem";
    return nullptr;
}

static std::vector<std::filesystem::path> iconFolderCandidates(const std::string& folder) {
    return {
        std::filesystem::path("textures/icons") / folder,
        std::filesystem::path("HeatSpectra/textures/icons") / folder,
        std::filesystem::path("../textures/icons") / folder,
        std::filesystem::path("../../textures/icons") / folder,
        std::filesystem::path("../HeatSpectra/textures/icons") / folder,
        std::filesystem::path("../../HeatSpectra/textures/icons") / folder
    };
}

static int parseIconWidth(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    if (name.size() < 2 || name.back() != 'w') {
        return 0;
    }

    try {
        return std::stoi(name.substr(0, name.size() - 1));
    } catch (...) {
        return 0;
    }
}

static std::string resolveIconPath(const std::string& folder, int targetWidth) {
    std::vector<std::pair<int, std::filesystem::path>> variants;

    for (const auto& folderPath : iconFolderCandidates(folder)) {
        if (!std::filesystem::exists(folderPath) || !std::filesystem::is_directory(folderPath)) {
            continue;
        }

        for (const auto& sizeEntry : std::filesystem::directory_iterator(folderPath)) {
            if (!sizeEntry.is_directory()) {
                continue;
            }

            const int variantWidth = parseIconWidth(sizeEntry.path());
            if (variantWidth <= 0) {
                continue;
            }

            for (const auto& fileEntry : std::filesystem::directory_iterator(sizeEntry.path())) {
                if (!fileEntry.is_regular_file()) {
                    continue;
                }

                const std::filesystem::path extension = fileEntry.path().extension();
                if (extension == ".png" || extension == ".PNG") {
                    variants.emplace_back(variantWidth, fileEntry.path());
                    break;
                }
            }
        }
    }

    if (variants.empty()) {
        return {};
    }

    std::sort(
        variants.begin(),
        variants.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

    for (const auto& variant : variants) {
        if (variant.first >= targetWidth) {
            return variant.second.string();
        }
    }

    return variants.back().second.string();
}

bool IconCacheKey::operator==(const IconCacheKey& other) const noexcept {
    return typeId == other.typeId && iconWidthKey == other.iconWidthKey;
}

std::size_t IconCacheKeyHash::operator()(const IconCacheKey& key) const noexcept {
    const std::size_t typeHash = std::hash<NodeTypeId>{}(key.typeId);
    const std::size_t widthHash = std::hash<int>{}(key.iconWidthKey);
    return typeHash ^ (widthHash + 0x9e3779b9 + (typeHash << 6) + (typeHash >> 2));
}

QString NodeGraphIconRegistry::iconPathForType(const NodeTypeId& typeId, qreal targetPixelWidth) {
    const char* folder = iconFolderForType(typeId);
    if (!folder) {
        return QString();
    }

    const std::string path = resolveIconPath(folder, iconWidthCacheKey(targetPixelWidth));
    return path.empty() ? QString() : QString::fromStdString(path);
}

QPixmap NodeGraphIconRegistry::iconForType(const NodeTypeId& typeId, qreal targetPixelWidth) {
    const QString iconPath = iconPathForType(typeId, targetPixelWidth);
    if (iconPath.isEmpty()) {
        return QPixmap();
    }

    const std::filesystem::path iconFilePath(iconPath.toStdString());
    const IconCacheKey cacheKey{
        typeId,
        parseIconWidth(iconFilePath.parent_path().filename())
    };
    const auto cacheIt = iconCache.find(cacheKey);
    if (cacheIt != iconCache.end()) {
        return cacheIt->second;
    }

    QPixmap pixmap(iconPath);
    if (pixmap.isNull()) {
        return QPixmap();
    }

    iconCache.emplace(cacheKey, pixmap);
    return pixmap;
}

int NodeGraphIconRegistry::iconWidthCacheKey(qreal targetPixelWidth) {
    return std::max(1, static_cast<int>(std::lround(targetPixelWidth)));
}
