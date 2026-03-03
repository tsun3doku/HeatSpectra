#include "GlyphText.hpp"

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
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());

    atlasWidthPx = GlyphText::readJsonNumber(json, "\"scaleW\":", 856.0f);
    atlasHeightPx = GlyphText::readJsonNumber(json, "\"scaleH\":", 64.0f);
    if (atlasWidthPx <= 0.0f) {
        atlasWidthPx = 856.0f;
    }
    if (atlasHeightPx <= 0.0f) {
        atlasHeightPx = 64.0f;
    }

    size_t cursor = json.find("\"chars\"");
    cursor = (cursor == std::string::npos) ? cursor : json.find('[', cursor);
    if (cursor == std::string::npos) {
        std::cerr << "[GlyphText] Missing chars array in font metadata: " << atlasMetadataPath << std::endl;
        return false;
    }

    while (true) {
        const size_t start = json.find('{', cursor);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = json.find('}', start);
        if (end == std::string::npos) {
            break;
        }

        const std::string item = json.substr(start, end - start + 1);
        const int id = static_cast<int>(GlyphText::readJsonNumber(item, "\"id\":", -1.0f));

        if (id >= 0) {
            const size_t index = static_cast<size_t>(id);
            if (index >= charMap.size()) {
                charMap.resize(index + 1);
            }

            CharInfo info{};
            const float x = GlyphText::readJsonNumber(item, "\"x\":", 0.0f);
            const float y = GlyphText::readJsonNumber(item, "\"y\":", 0.0f);
            info.width = GlyphText::readJsonNumber(item, "\"width\":", 0.0f);
            info.height = GlyphText::readJsonNumber(item, "\"height\":", 0.0f);
            info.xadvance = GlyphText::readJsonNumber(item, "\"xadvance\":", info.width);
            info.xoffset = GlyphText::readJsonNumber(item, "\"xoffset\":", 0.0f);
            info.yoffset = GlyphText::readJsonNumber(item, "\"yoffset\":", 0.0f);
            info.u = x / atlasWidthPx;
            info.v = y / atlasHeightPx;
            charMap[index] = info;
        }

        cursor = end + 1;
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

float GlyphText::readJsonNumber(const std::string& text, const std::string& key, float fallback) {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return fallback;
    }

    size_t pos = keyPos + key.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }

    size_t end = pos;
    while (end < text.size()) {
        const char c = text[end];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            ++end;
            continue;
        }
        break;
    }

    if (end <= pos) {
        return fallback;
    }

    return std::stof(text.substr(pos, end - pos));
}
