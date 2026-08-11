#include "GlyphText.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <utility>

GlyphText::GlyphText(std::string atlasTexturePath, std::string atlasMetadataPath)
    : atlasTexturePath(std::move(atlasTexturePath)),
      atlasMetadataPath(std::move(atlasMetadataPath)) {
}

bool GlyphText::load() {
    charMap.assign(128, {});

    std::ifstream jsonFile(atlasMetadataPath);
    if (!jsonFile.is_open()) {
        std::cerr << "[GlyphText] Failed to open font metadata json: " << atlasMetadataPath << std::endl;
        return false;
    }
    const std::string jsonText((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(jsonText), &parseError);
    if (document.isNull() || !document.isObject()) {
        std::cerr << "[GlyphText] Invalid font metadata json: " << atlasMetadataPath
                  << " (" << parseError.errorString().toStdString() << ")" << std::endl;
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject atlas = root.value("atlas").toObject();
    if (atlas.isEmpty() || root.value("glyphs").toArray().isEmpty()) {
        std::cerr << "[GlyphText] Expected native msdf-atlas-gen JSON: "
                  << atlasMetadataPath << std::endl;
        return false;
    }
    if (atlas.value("yOrigin").toString() != "top") {
        std::cerr << "[GlyphText] Font atlas must be generated with -yorigin top: "
                  << atlasMetadataPath << std::endl;
        return false;
    }

    atlasWidthPx = static_cast<float>(atlas.value("width").toDouble());
    atlasHeightPx = static_cast<float>(atlas.value("height").toDouble());
    atlasEmSizePx = static_cast<float>(atlas.value("size").toDouble());
    const QJsonObject grid = atlas.value("grid").toObject();
    cellWidthPx = static_cast<float>(grid.value("cellWidth").toDouble());
    cellHeightPx = static_cast<float>(grid.value("cellHeight").toDouble());
    cellOriginXEm = static_cast<float>(grid.value("originX").toDouble());
    cellOriginYEm = static_cast<float>(grid.value("originY").toDouble());
    if (atlasWidthPx <= 0.0f || atlasHeightPx <= 0.0f || atlasEmSizePx <= 0.0f ||
        cellWidthPx <= 0.0f || cellHeightPx <= 0.0f) {
        std::cerr << "[GlyphText] Invalid atlas dimensions in: " << atlasMetadataPath << std::endl;
        return false;
    }

    for (const QJsonValue glyphValue : root.value("glyphs").toArray()) {
        const QJsonObject glyph = glyphValue.toObject();
        const int id = glyph.value("unicode").toInt(-1);
        if (id < 0) continue;

        const size_t index = static_cast<size_t>(id);
        if (index >= charMap.size()) charMap.resize(index + 1);

        CharInfo info{};
        info.advanceEm = static_cast<float>(glyph.value("advance").toDouble());
        const QJsonObject plane = glyph.value("planeBounds").toObject();
        const QJsonObject bounds = glyph.value("atlasBounds").toObject();
        if (!plane.isEmpty() && !bounds.isEmpty()) {
            const float left = static_cast<float>(bounds.value("left").toDouble());
            const float top = static_cast<float>(bounds.value("top").toDouble());
            const float right = static_cast<float>(bounds.value("right").toDouble());
            const float bottom = static_cast<float>(bounds.value("bottom").toDouble());
            info.u = left / atlasWidthPx;
            info.v = top / atlasHeightPx;
            info.width = right - left;
            info.height = bottom - top;
            info.planeLeft = static_cast<float>(plane.value("left").toDouble());
            info.planeTop = static_cast<float>(plane.value("top").toDouble());
            info.planeRight = static_cast<float>(plane.value("right").toDouble());
            info.planeBottom = static_cast<float>(plane.value("bottom").toDouble());
            if (planeHeightEm <= 0.0f) {
                planeHeightEm = info.planeBottom - info.planeTop;
            }
        }
        charMap[index] = info;
    }
    if (planeHeightEm <= 0.0f) {
        std::cerr << "[GlyphText] Invalid uniform-grid glyph bounds in: "
                  << atlasMetadataPath << std::endl;
        return false;
    }
    return true;
}

const GlyphText::CharInfo& GlyphText::getCharInfo(char c) const {
    const uint32_t index = static_cast<uint32_t>(static_cast<unsigned char>(c));
    if (index >= charMap.size()) {
        return zeroChar;
    }
    return charMap[index];
}

glm::vec4 GlyphText::getCharUV(char c) const {
    const CharInfo& info = getCharInfo(c);
    if (info.width <= 0.0f || info.height <= 0.0f || atlasWidthPx <= 0.0f || atlasHeightPx <= 0.0f) {
        return glm::vec4(0.0f);
    }

    return glm::vec4(info.u, info.v, info.width / atlasWidthPx, info.height / atlasHeightPx);
}

bool GlyphText::hasGlyph(char c) const {
    const CharInfo& info = getCharInfo(c);
    return info.width > 0.0f && info.height > 0.0f;
}
