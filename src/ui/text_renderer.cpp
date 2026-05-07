#include "text_renderer.hpp"
#include "../core/renderer.hpp"
#include <algorithm>

TextRenderer::TextRenderer(Renderer& r) 
    : renderer(r), defaultShadow(false), defaultShadowColor(0.0f, 0.0f, 0.0f) {
}

float TextRenderer::getTextWidth(const std::string& text, float scale) {
    // Each character is 3 pixels wide + 1 spacing = 4 pixels
    // With scale factor and additional spacing
    float charWidth = 8.0f * scale * 4.0f;  // pixelSize * (3 + 1 spacing)
    float charSpacing = 8.0f * scale * 2.0f; // Extra spacing between chars
    return text.length() * (charWidth + charSpacing);
}

float TextRenderer::getTextHeight(float scale) {
    // Each character is 5 pixels tall + 1 spacing = 6 pixels
    return 8.0f * scale * 6.0f; // pixelSize * (5 + 1 spacing)
}

void TextRenderer::drawText(const std::string& text, float x, float y, const TextStyle& style) {
    drawTextInternal(text, x, y, style);
}

void TextRenderer::drawText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    TextStyle style(scale, color);
    drawTextInternal(text, x, y, style);
}

void TextRenderer::drawTextAligned(const std::string& text, float x, float y, TextAlign align, 
                                  const TextStyle& style) {
    float textWidth = getTextWidth(text, style.scale);
    
    float drawX = x;
    switch (align) {
        case TextAlign::LEFT:
            drawX = x;
            break;
        case TextAlign::CENTER:
            drawX = x - textWidth / 2.0f;
            break;
        case TextAlign::RIGHT:
            drawX = x - textWidth;
            break;
    }
    
    drawTextInternal(text, drawX, y, style);
}

void TextRenderer::drawSmall(const std::string& text, float x, float y, const glm::vec3& color) {
    drawText(text, x, y, TextStyle::Small().withColor(color));
}

void TextRenderer::drawNormal(const std::string& text, float x, float y, const glm::vec3& color) {
    drawText(text, x, y, TextStyle::Normal().withColor(color));
}

void TextRenderer::drawLarge(const std::string& text, float x, float y, const glm::vec3& color) {
    drawText(text, x, y, TextStyle::Large().withColor(color));
}

void TextRenderer::drawTitle(const std::string& text, float x, float y, const glm::vec3& color) {
    drawText(text, x, y, TextStyle::Title().withColor(color));
}

void TextRenderer::drawHeader(const std::string& text, float x, float y, const glm::vec3& color) {
    drawText(text, x, y, TextStyle::Header().withColor(color));
}

void TextRenderer::drawMultiline(const std::string& text, float x, float y, float lineHeight,
                                  const TextStyle& style) {
    size_t start = 0;
    size_t end = 0;
    float currentY = y;
    
    while (end != std::string::npos) {
        end = text.find('\n', start);
        std::string line = text.substr(start, end - start);
        drawTextInternal(line, x, currentY, style);
        currentY += lineHeight;
        start = end + 1;
    }
}

void TextRenderer::drawTextInternal(const std::string& text, float x, float y, const TextStyle& style) {
    // Draw shadow if enabled
    if (style.shadow) {
        // Note: The renderer's drawText doesn't support color directly,
        // so we'd need to modify the renderer to support colored text.
        // For now, just draw at a slight offset without color change
        renderer.drawText(text, x + 1.0f, y + 1.0f, style.scale);
    }
    
    // Draw main text
    renderer.drawText(text, x, y, style.scale);
}
