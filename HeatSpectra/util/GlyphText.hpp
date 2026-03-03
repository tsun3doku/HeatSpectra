#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

class GlyphText {
public:
    struct CharInfo {
        float u = 0.0f;
        float v = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float xadvance = 0.0f;
        float xoffset = 0.0f;
        float yoffset = 0.0f;
    };

    GlyphText(
        std::string atlasTexturePath = "textures/Roboto-Medium-timing.png",
        std::string atlasMetadataPath = "textures/Roboto-Medium-timing.json");

    bool load();

    const std::string& getAtlasTexturePath() const { return atlasTexturePath; }
    const std::string& getAtlasMetadataPath() const { return atlasMetadataPath; }

    float getAtlasWidth() const { return atlasWidthPx; }
    float getAtlasHeight() const { return atlasHeightPx; }

    const CharInfo& getCharInfo(char c) const;
    glm::vec4 getCharUV(char c) const;
    bool hasGlyph(char c) const;

private:
    static float readJsonNumber(const std::string& text, const std::string& key, float fallback);

    std::string atlasTexturePath;
    std::string atlasMetadataPath;

    float atlasWidthPx = 856.0f;
    float atlasHeightPx = 64.0f;

    std::vector<CharInfo> charMap;
    CharInfo zeroChar{};
};
