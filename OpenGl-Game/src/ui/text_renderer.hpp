#ifndef TEXT_RENDERER_HPP
#define TEXT_RENDERER_HPP

#include <string>
#include <glm/glm.hpp>

// Forward declaration
class Renderer;

// Text rendering options
struct TextStyle {
    float scale;
    glm::vec3 color;
    bool shadow;
    glm::vec3 shadowColor;
    
    TextStyle(float s = 1.0f, glm::vec3 c = glm::vec3(1.0f))
        : scale(s), color(c), shadow(false), shadowColor(0.0f, 0.0f, 0.0f) {}
    
    static TextStyle Small() { return TextStyle(0.8f); }
    static TextStyle Normal() { return TextStyle(1.0f); }
    static TextStyle Large() { return TextStyle(1.5f); }
    static TextStyle Title() { return TextStyle(2.0f); }
    static TextStyle Header() { return TextStyle(1.2f); }
    
    TextStyle& withColor(const glm::vec3& c) { color = c; return *this; }
};

// Text alignment
enum class TextAlign {
    LEFT,
    CENTER,
    RIGHT
};

// Text renderer class for easy text rendering
class TextRenderer {
public:
    TextRenderer(Renderer& renderer);
    
    // Basic text drawing
    void drawText(const std::string& text, float x, float y, const TextStyle& style = TextStyle::Normal());
    void drawText(const std::string& text, float x, float y, float scale, const glm::vec3& color = glm::vec3(1.0f));
    
    // Aligned text drawing
    void drawTextAligned(const std::string& text, float x, float y, TextAlign align, 
                         const TextStyle& style = TextStyle::Normal());
    
    // Convenience methods for common text styles
    void drawSmall(const std::string& text, float x, float y, const glm::vec3& color = glm::vec3(1.0f));
    void drawNormal(const std::string& text, float x, float y, const glm::vec3& color = glm::vec3(1.0f));
    void drawLarge(const std::string& text, float x, float y, const glm::vec3& color = glm::vec3(1.0f));
    void drawTitle(const std::string& text, float x, float y, const glm::vec3& color = glm::vec3(1.0f));
    void drawHeader(const std::string& text, float x, float y, const glm::vec3& color = glm::vec3(1.0f));
    
    // Multi-line text
    void drawMultiline(const std::string& text, float x, float y, float lineHeight,
                       const TextStyle& style = TextStyle::Normal());
    
    // Text measurement
    float getTextWidth(const std::string& text, float scale = 1.0f);
    float getTextHeight(float scale = 1.0f);
    
    // Shadow settings
    void setDefaultShadow(bool enabled) { defaultShadow = enabled; }
    void setShadowColor(const glm::vec3& color) { defaultShadowColor = color; }
    
private:
    Renderer& renderer;
    bool defaultShadow;
    glm::vec3 defaultShadowColor;
    
    void drawTextInternal(const std::string& text, float x, float y, const TextStyle& style);
};

#endif // TEXT_RENDERER_HPP
