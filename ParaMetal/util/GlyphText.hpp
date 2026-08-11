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
        float advanceEm = 0.0f;
        float planeLeft = 0.0f;
        float planeTop = 0.0f;
        float planeRight = 0.0f;
        float planeBottom = 0.0f;
    };

    struct GlyphInstance {
        glm::vec2 centerPx;
        glm::vec2 sizePx;
        glm::vec4 charUV;
        glm::vec4 color;
    };

    GlyphText(
        std::string atlasTexturePath = "textures/Roboto-Medium-timing.png",
        std::string atlasMetadataPath = "textures/Roboto-Medium-timing.json");

    bool load();

    const std::string& getAtlasTexturePath() const { return atlasTexturePath; }
    const std::string& getAtlasMetadataPath() const { return atlasMetadataPath; }

    float getAtlasWidth() const { return atlasWidthPx; }
    float getAtlasHeight() const { return atlasHeightPx; }
    float getAtlasEmSize() const { return atlasEmSizePx; }
    float getCellWidth() const { return cellWidthPx; }
    float getCellHeight() const { return cellHeightPx; }
    float getCellOriginX() const { return cellOriginXEm; }
    float getCellOriginY() const { return cellOriginYEm; }
    float getPlaneHeight() const { return planeHeightEm; }

    const CharInfo& getCharInfo(char c) const;
    glm::vec4 getCharUV(char c) const;
    bool hasGlyph(char c) const;

private:
    std::string atlasTexturePath;
    std::string atlasMetadataPath;

    float atlasWidthPx = 856.0f;
    float atlasHeightPx = 64.0f;
    float atlasEmSizePx = 32.0f;
    float cellWidthPx = 64.0f;
    float cellHeightPx = 64.0f;
    float cellOriginXEm = 0.0f;
    float cellOriginYEm = 0.0f;
    float planeHeightEm = 0.0f;

    std::vector<CharInfo> charMap;
    CharInfo zeroChar{};
};
