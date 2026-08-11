#include "TimingRenderer.hpp"

#include "ScreenTextRenderer.hpp"

TimingRenderer::TimingRenderer(ScreenTextRenderer& renderer)
    : textRenderer(renderer) {
}

TimingRenderer::~TimingRenderer() = default;

void TimingRenderer::buildGlyphInstances() {
    glyphInstances.clear();
    const GlyphText& glyphText = textRenderer.getGlyphText();
    const float emScale = 18.0f / glyphText.getPlaneHeight();
    const float marginX = 8.0f;
    const float marginY = 8.0f;
    const float lineSpacing = 18.0f;
    const glm::vec4 labelColor(0.82f, 0.84f, 0.86f, 1.0f);
    const glm::vec4 valueColor(0.2f, 0.75f, 0.25f, 1.0f);

    float lineTop = marginY;
    for (const std::string& line : activeLines) {
        const size_t separatorPosition = line.find(':');
        float cursorX = marginX + glyphText.getCellOriginX() * emScale;
        const float baselineY = lineTop + glyphText.getCellOriginY() * emScale;
        for (size_t characterIndex = 0; characterIndex < line.size(); ++characterIndex) {
            const char character = line[characterIndex];
            const GlyphText::CharInfo& info = glyphText.getCharInfo(character);
            if (info.width > 0.0f && info.height > 0.0f) {
                GlyphText::GlyphInstance glyph{};
                glyph.centerPx = glm::vec2(
                    cursorX + 0.5f * (info.planeLeft + info.planeRight) * emScale,
                    baselineY + 0.5f * (info.planeTop + info.planeBottom) * emScale);
                glyph.sizePx = glm::vec2(
                    (info.planeRight - info.planeLeft) * emScale,
                    (info.planeBottom - info.planeTop) * emScale);
                glyph.charUV = glyphText.getCharUV(character);
                glyph.color = separatorPosition != std::string::npos && characterIndex > separatorPosition
                    ? valueColor
                    : labelColor;
                if (glyph.charUV.z > 0.0f && glyph.charUV.w > 0.0f) {
                    glyphInstances.push_back(glyph);
                    if (glyphInstances.size() >= maxGlyphCapacity) {
                        break;
                    }
                }
            }
            cursorX += info.advanceEm * emScale;
        }
        if (glyphInstances.size() >= maxGlyphCapacity) {
            break;
        }
        lineTop += lineSpacing;
    }
}

void TimingRenderer::setLines(const std::vector<std::string>& lines) {
    if (lines == activeLines) {
        return;
    }
    activeLines = lines;
    buildGlyphInstances();
}

void TimingRenderer::render(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkExtent2D extent) {
    textRenderer.draw(commandBuffer, currentFrame, extent, glyphInstances);
}

void TimingRenderer::cleanup() {
    activeLines.clear();
    glyphInstances.clear();
}
