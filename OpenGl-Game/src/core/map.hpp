/**
 * @file map.hpp
 * @brief Map class for managing 3D game maps with textures
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef MAP_HPP
#define MAP_HPP

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "texture.hpp"

/**
 * @brief Structure representing a map block/element
 */
struct MapBlock {
    /// Position of the block in world space
    glm::vec3 position;
    /// Scale of the block
    glm::vec3 scale;
    /// Texture ID for this block type
    int textureId;
    /// Block type identifier
    int blockType;
    /// Whether the block is solid (can collide)
    bool solid;
    /// Color of the block (RGB)
    glm::vec3 color;
    
    MapBlock(glm::vec3 pos = glm::vec3(0.0f), 
             glm::vec3 scl = glm::vec3(1.0f), 
             int texId = 0, 
             int type = 0, 
             bool isSolid = true,
             glm::vec3 col = glm::vec3(0.7f, 0.7f, 0.7f))
        : position(pos), scale(scl), textureId(texId), blockType(type), solid(isSolid), color(col) {}
};

/**
 * @brief Map class for managing 3D game maps
 * 
 * Handles loading maps from files, managing textures,
 * and providing map data for rendering and collision detection.
 */
class Map {
public:
    /**
     * @brief Construct a new Map object
     * 
     * @param mapFile Path to the map file (optional)
     */
    Map(const std::string& mapFile = "");
    
    /**
     * @brief Destructor - cleans up map resources
     */
    ~Map();
    
    /**
     * @brief Load map from file
     * 
     * @param filename Path to the map file
     * @return true if loaded successfully, false otherwise
     */
    bool loadFromFile(const std::string& filename);
    
    /**
     * @brief Load a texture for map blocks
     * 
     * @param texturePath Path to the texture file
     * @param textureId ID to assign to this texture
     * @return true if loaded successfully, false otherwise
     */
    bool loadTexture(const std::string& texturePath, int textureId);
    
    /**
     * @brief Add a block to the map
     * 
     * @param block MapBlock to add
     */
    void addBlock(const MapBlock& block);
    
    /**
     * @brief Get all blocks in the map
     * 
     * @return const std::vector<MapBlock>& Reference to blocks vector
     */
    const std::vector<MapBlock>& getBlocks() const;
    
    /**
     * @brief Get texture by ID
     * 
     * @param textureId Texture ID
     * @return Texture* Pointer to texture, or nullptr if not found
     */
    Texture* getTexture(int textureId);
    
    /**
     * @brief Check collision with map blocks
     * 
     * @param position Position to check
     * @param radius Collision radius
     * @return true if collision detected, false otherwise
     */
    bool checkCollision(const glm::vec3& position, float radius = 0.5f) const;
    
    /**
     * @brief Get map dimensions
     * 
     * @return glm::vec3 Map dimensions (width, height, depth)
     */
    glm::vec3 getDimensions() const;
    
    /**
     * @brief Clear all blocks from the map
     */
    void clear();

private:
    /// All blocks in the map
    std::vector<MapBlock> blocks;
    /// Available textures
    std::vector<Texture*> textures;
    /// Map dimensions
    glm::vec3 dimensions;
    /// Map name
    std::string mapName;
    
    /**
     * @brief Parse map data from string
     * 
     * @param data Map data string
     * @return true if parsed successfully, false otherwise
     */
    bool parseMapData(const std::string& data);
    
    /**
     * @brief Generate a simple test map
     */
    void generateTestMap();
};

#endif
