/**
 * @file map.cpp
 * @brief Implementation of Map class for managing 3D game maps
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "map.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

Map::Map(const std::string& mapFile) 
    : dimensions(20.0f, 5.0f, 20.0f), mapName("Default") {
    
    if (!mapFile.empty()) {
        loadFromFile(mapFile);
    } else {
        generateTestMap();
    }
}

Map::~Map() {
    clear();
}

bool Map::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open map file: " << filename << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return parseMapData(buffer.str());
}

bool Map::parseMapData(const std::string& data) {
    std::istringstream stream(data);
    std::string line;
    
    clear();
    
    while (std::getline(stream, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream lineStream(line);
        std::string command;
        lineStream >> command;
        
        if (command == "texture") {
            std::string texturePath;
            int textureId;
            lineStream >> texturePath >> textureId;
            loadTexture(texturePath, textureId);
        }
        else if (command == "block") {
            float x, y, z, sx, sy, sz;
            int textureId, blockType;
            bool solid;
            float r, g, b;
            
            // Try to parse with color (new format)
            lineStream >> x >> y >> z >> sx >> sy >> sz >> textureId >> blockType >> solid;
            
            // Check if color values are present
            if (lineStream >> r >> g >> b) {
                // New format with color
                addBlock(MapBlock(glm::vec3(x, y, z), glm::vec3(sx, sy, sz), 
                                 textureId, blockType, solid, glm::vec3(r, g, b)));
            } else {
                // Old format - assign default colors based on blockType
                glm::vec3 defaultColors[] = {
                    glm::vec3(0.2f, 0.8f, 0.2f),   // 0: Green
                    glm::vec3(0.6f, 0.6f, 0.6f),   // 1: Gray
                    glm::vec3(0.8f, 0.3f, 0.2f),   // 2: Red-brown
                    glm::vec3(0.9f, 0.7f, 0.4f),   // 3: Tan
                    glm::vec3(0.4f, 0.4f, 0.7f),   // 4: Blue-gray
                    glm::vec3(0.6f, 0.5f, 0.3f),   // 5: Wood
                    glm::vec3(0.3f, 0.7f, 0.8f),   // 6: Cyan
                    glm::vec3(0.7f, 0.7f, 0.7f)    // 7: Default gray
                };
                glm::vec3 color = defaultColors[blockType % 8];
                
                // Ensure ALL blocks have collision
                addBlock(MapBlock(glm::vec3(x, y, z), glm::vec3(sx, sy, sz), 
                                 textureId, blockType, true, color)); // Force solid=true
            }
        }
        else if (command == "dimensions") {
            float w, h, d;
            lineStream >> w >> h >> d;
            dimensions = glm::vec3(w, h, d);
        }
        else if (command == "name") {
            std::string name;
            lineStream >> name;
            mapName = name;
        }
    }
    
    std::cout << "Loaded map: " << mapName << " with " << blocks.size() << " blocks" << std::endl;
    return true;
}

bool Map::loadTexture(const std::string& texturePath, int textureId) {
    // Ensure textures vector is large enough
    while (textures.size() <= static_cast<size_t>(textureId)) {
        textures.push_back(nullptr);
    }
    
    // Delete existing texture if present
    if (textures[textureId] != nullptr) {
        delete textures[textureId];
    }
    
    // Create new texture with appropriate texture unit
    GLenum textureUnit = GL_TEXTURE0 + textureId;
    std::string fullPath = "assets/textures/" + texturePath;
    textures[textureId] = new Texture(fullPath.c_str(), textureUnit);
    
    return textures[textureId]->ID != 0;
}

void Map::addBlock(const MapBlock& block) {
    blocks.push_back(block);
}

bool Map::deleteBlockAt(const glm::vec3& position, float tolerance) {
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        // Check if the position is within this block's bounds
        glm::vec3 blockMin = it->position - it->scale * 0.5f;
        glm::vec3 blockMax = it->position + it->scale * 0.5f;
        
        if (position.x >= blockMin.x && position.x <= blockMax.x &&
            position.y >= blockMin.y && position.y <= blockMax.y &&
            position.z >= blockMin.z && position.z <= blockMax.z) {
            blocks.erase(it);
            return true;
        }
    }
    return false;
}

const std::vector<MapBlock>& Map::getBlocks() const {
    return blocks;
}

Texture* Map::getTexture(int textureId) {
    if (textureId >= 0 && static_cast<size_t>(textureId) < textures.size()) {
        return textures[textureId];
    }
    return nullptr;
}

bool Map::checkCollision(const glm::vec3& position, float radius) const {
    for (const auto& block : blocks) {
        if (!block.solid) continue;
        
        // Simple AABB collision detection
        glm::vec3 blockMin = block.position - block.scale * 0.5f;
        glm::vec3 blockMax = block.position + block.scale * 0.5f;
        glm::vec3 posMin = position - glm::vec3(radius);
        glm::vec3 posMax = position + glm::vec3(radius);
        
        if (posMin.x < blockMax.x && posMax.x > blockMin.x &&
            posMin.y < blockMax.y && posMax.y > blockMin.y &&
            posMin.z < blockMax.z && posMax.z > blockMin.z) {
            return true;
        }
    }
    return false;
}

glm::vec3 Map::getDimensions() const {
    return dimensions;
}

void Map::clear() {
    blocks.clear();
    
    // Clean up textures
    for (auto* texture : textures) {
        if (texture != nullptr) {
            delete texture;
        }
    }
    textures.clear();
}

void Map::generateTestMap() {
    mapName = "Test Map";
    
    // Create a simple floor (now solid for collision)
    for (int x = -5; x <= 5; x++) {
        for (int z = -5; z <= 5; z++) {
            addBlock(MapBlock(glm::vec3(x, -1.0f, z), glm::vec3(1.0f, 1.0f, 1.0f), 0, 0, true, glm::vec3(0.2f, 0.8f, 0.2f)));
        }
    }
    
    // Add some walls
    for (int x = -5; x <= 5; x++) {
        addBlock(MapBlock(glm::vec3(x, 0.0f, -6.0f), glm::vec3(1.0f, 2.0f, 0.2f), 1, 1, true, glm::vec3(0.6f, 0.6f, 0.6f)));
        addBlock(MapBlock(glm::vec3(x, 0.0f, 6.0f), glm::vec3(1.0f, 2.0f, 0.2f), 1, 1, true, glm::vec3(0.6f, 0.6f, 0.6f)));
    }
    
    for (int z = -5; z <= 5; z++) {
        addBlock(MapBlock(glm::vec3(-6.0f, 0.0f, z), glm::vec3(0.2f, 2.0f, 1.0f), 1, 1, true, glm::vec3(0.6f, 0.6f, 0.6f)));
        addBlock(MapBlock(glm::vec3(6.0f, 0.0f, z), glm::vec3(0.2f, 2.0f, 1.0f), 1, 1, true, glm::vec3(0.6f, 0.6f, 0.6f)));
    }
    
    // Add some decorative blocks
    addBlock(MapBlock(glm::vec3(2.0f, -0.75f, 2.0f), glm::vec3(1.2f, 0.5f, 1.2f), 2, 2, true, glm::vec3(0.8f, 0.3f, 0.2f)));
    
    std::cout << "Generated test map with " << blocks.size() << " blocks" << std::endl;
}
