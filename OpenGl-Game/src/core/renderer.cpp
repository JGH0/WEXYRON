/**
 * @file renderer.cpp
 * @brief Implementation of Renderer class for OpenGL rendering operations
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "renderer.hpp"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

void Renderer::setViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

bool Renderer::initialize(int width, int height) {
    // Set clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    std::cout << "Renderer initialized successfully!" << std::endl;
    return true;
}

void Renderer::beginFrame() {
    // Clear the screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() {
    // Frame ending handled by window swapBuffers
}

void Renderer::shutdown() {
    // TODO: Implement renderer cleanup
}

/// Vertex data for a colored cube (positions and colors)
static const float cubeVertices[] = {
    // positions          // colors
    -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f
};

/// Vertex data for a textured cube (positions and texture coordinates)
static const float texturedCubeVertices[] = {
    // positions          // texture coords
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

    -0.5f,  0.5f, -0.5f,  0.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 0.0f
};

Renderer::Renderer() : shader(nullptr), texturedShader(nullptr) {}

Renderer::~Renderer() {
    if (shader) delete shader;
    if (texturedShader) delete texturedShader;
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
}

void Renderer::init() {
    shader = new Shader("assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl");
    texturedShader = new Shader("assets/shaders/textured_vertex.glsl", "assets/shaders/textured_fragment.glsl");

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);

    glBindVertexArray(cubeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

void Renderer::setViewProjection(const glm::mat4& view, const glm::mat4& projection) {
    currentView = view;
    currentProjection = projection;
}

void Renderer::drawCube(const glm::mat4& model, const glm::vec3& color) {
    shader->use();
    shader->setMat4("model", glm::value_ptr(model));
    shader->setMat4("view", glm::value_ptr(currentView));
    shader->setMat4("projection", glm::value_ptr(currentProjection));
    // Set the object color uniform
    glUniform3f(glGetUniformLocation(shader->ID, "objectColor"), color.r, color.g, color.b);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void Renderer::clear() {
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawTexturedCube(const glm::mat4& model, Texture* texture) {
    if (!texture || !texturedShader) return;
    
    // Bind texture - this activates the texture unit and binds the texture
    texture->bind();
    
    // Use textured shader
    texturedShader->use();
    texturedShader->setMat4("model", glm::value_ptr(model));
    texturedShader->setMat4("view", glm::value_ptr(currentView));
    texturedShader->setMat4("projection", glm::value_ptr(currentProjection));
    
    // Tell shader which texture unit to sample from
    // Texture unit is stored as GL_TEXTURE0 + unit, so we need to extract the unit number
    GLint unit = texture->getUnit() - GL_TEXTURE0;
    texturedShader->setInt("ourTexture", unit);
    
    // Bind VAO first, then update vertex data and attributes
    glBindVertexArray(cubeVAO);
    
    // Update vertex buffer data for textured rendering
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texturedCubeVertices), texturedCubeVertices, GL_STATIC_DRAW);
    
    // position attribute (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // texture coord attribute (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Draw
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    
    // Unbind texture
    texture->unbind();
}

void Renderer::renderMap(Map* map) {
    if (!map) return;
    
    const auto& blocks = map->getBlocks();
    
    for (const auto& block : blocks) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, block.position);
        model = glm::scale(model, block.scale);
        
        Texture* texture = map->getTexture(block.textureId);
        if (texture) {
            drawTexturedCube(model, texture);
        } else {
            // Use the block's defined color
            drawCube(model, block.color);
        }
    }
}

void Renderer::drawColoredQuad(const glm::mat4& model, const glm::vec3& color) {
    // Use to colored shader for 2D quad rendering
    shader->use();
    shader->setMat4("model", glm::value_ptr(model));
    shader->setMat4("view", glm::value_ptr(currentView));
    shader->setMat4("projection", glm::value_ptr(currentProjection));
    shader->setMat4("objectColor", glm::value_ptr(glm::mat4(1.0f))); // Use a dummy matrix for color
    
    // Set the color uniform directly (we need to add this to shader or use existing method)
    // For now, let's use the existing drawCube method with a very thin cube
    glm::mat4 quadModel = glm::scale(model, glm::vec3(1.0f, 1.0f, 0.001f)); // Make it very thin
    drawCube(quadModel, color);
}

void Renderer::drawText(const std::string& text, float x, float y, float scale) {
    // Disable depth test for text to ensure it appears on top
    glDisable(GL_DEPTH_TEST);
    
    // Draw letters using 3x5 bitmap patterns
    float pixelSize = 8.0f * scale;  // Size of each pixel block
    float charWidth = pixelSize * 4;  // 3 pixels + 1 spacing
    float charHeight = pixelSize * 6; // 5 pixels + 1 spacing
    (void)charHeight; // Suppress unused variable warning
    float charSpacing = pixelSize * 2; // Space between letters
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f); // WHITE text
    
    for (size_t i = 0; i < text.length(); i++) {
        char c = toupper(text[i]);
        float charX = x + i * (charWidth + charSpacing);
        float charY = y;
        
        if (c == ' ') continue;
        
        // 3x5 bitmap patterns for letters (1 = filled, 0 = empty)
        // 15 bits: row0 (bits 14-12), row1 (11-9), row2 (8-6), row3 (5-3), row4 (2-0)
        unsigned short pattern = 0;
        
        switch (c) {
            case 'A': pattern = 0b010101111101101; break;
            case 'B': pattern = 0b111101110101110; break;
            case 'C': pattern = 0b111100100100111; break;
            case 'D': pattern = 0b110101101101110; break;
            case 'E': pattern = 0b111100111100111; break;
            case 'F': pattern = 0b111100111100100; break;
            case 'G': pattern = 0b111100101101111; break;
            case 'H': pattern = 0b101101111101101; break;
            case 'I': pattern = 0b010010010010010; break;
            case 'J': pattern = 0b111001001101111; break;
            case 'K': pattern = 0b101101110101101; break;
            case 'L': pattern = 0b100100100100111; break;
            case 'M': pattern = 0b101111101101101; break;
            case 'N': pattern = 0b101111111111101; break;
            case 'O': pattern = 0b111101101101111; break;
            case 'P': pattern = 0b111101111100100; break;
            case 'Q': pattern = 0b111101101101011; break;
            case 'R': pattern = 0b110101110101101; break;
            case 'S': pattern = 0b111100111001111; break;
            case 'T': pattern = 0b111010010010010; break;
            case 'U': pattern = 0b101101101101111; break;
            case 'V': pattern = 0b101101101101010; break;
            case 'W': pattern = 0b101101111111101; break;
            case 'X': pattern = 0b101101010101101; break;
            case 'Y': pattern = 0b101101010010010; break;
            case 'Z': pattern = 0b111001010100111; break;
            case '0': pattern = 0b111101101101111; break;
            case '1': pattern = 0b010110010010111; break;
            case '2': pattern = 0b111001010100111; break;
            case '3': pattern = 0b111001111001111; break;
            case '4': pattern = 0b101101111001001; break;
            case '5': pattern = 0b111100111001111; break;
            case '6': pattern = 0b111100111101111; break;
            case '7': pattern = 0b111001010010100; break;
            case '8': pattern = 0b111101010101111; break;
            case '9': pattern = 0b111101111001111; break;
            case '/': pattern = 0b001001010100100; break;
            case '-': pattern = 0b000000111000000; break;
            case '_': pattern = 0b000000000000111; break;
            case '>': pattern = 0b000010001010000; break;
            case '<': pattern = 0b000010100010000; break;
            case '!': pattern = 0b010010010000010; break;
            case '?': pattern = 0b111001010000010; break;
            case '+': pattern = 0b000010111010000; break;
            case '=': pattern = 0b000111000111000; break;
            case '*': pattern = 0b101010101000000; break;
            case '.': pattern = 0b000000000000010; break;
            case ',': pattern = 0b000000000010100; break;
            case ':': pattern = 0b000010000010000; break;
            case ';': pattern = 0b000010000010100; break;
            case '|': pattern = 0b010010010010010; break;
            case '(': pattern = 0b010100100100010; break;
            case ')': pattern = 0b010001001001010; break;
            case '~': pattern = 0b000010111000000; break;
            default:  pattern = 0b010101010101010; break; // Default raster of black and white
        }
        
        // Draw the 3x5 pattern - row 0 is top
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                int bitPos = row * 3 + col;  // 0-14
                // Check if bit is set (from MSB to LSB)
                if (pattern & (1 << (14 - bitPos))) {
                    float pixelX = charX + col * pixelSize;
                    float pixelY = charY + row * pixelSize;
                    
                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, glm::vec3(pixelX + pixelSize/2, pixelY + pixelSize/2, 0.5f));
                    model = glm::scale(model, glm::vec3(pixelSize - 1, pixelSize - 1, 1.0f));
                    drawColoredQuad(model, color);
                }
            }
        }
    }
    
    // Re-enable depth test
    glEnable(GL_DEPTH_TEST);
}

// Helper function to create a quad model matrix
glm::mat4 Renderer::createQuadModel(float x, float y, float width, float height) {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(x, y, 0.0f));
    model = glm::scale(model, glm::vec3(width, height, 1.0f));
    return model;
}