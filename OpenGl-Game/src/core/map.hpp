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
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
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
    /// Y-axis rotation (degrees)
    float rotation;
    /// X-axis rotation (pitch) (degrees)
    float pitch;

    MapBlock(glm::vec3 pos = glm::vec3(0.0f),
             glm::vec3 scl = glm::vec3(1.0f),
             int texId = 0,
             int type = 0,
             bool isSolid = true,
             glm::vec3 col = glm::vec3(0.7f, 0.7f, 0.7f),
             float rot = 0.0f,
             float pit = 0.0f)
        : position(pos), scale(scl), textureId(texId), blockType(type), solid(isSolid), color(col), rotation(rot), pitch(pit) {}
};

/**
 * @brief Structure representing a generic spawn point (for Deathmatch)
 */
struct SpawnPoint {
    /// Position of the spawn point
    glm::vec3 position;
    /// Yaw rotation (horizontal facing direction)
    float yaw;
    /// Pitch rotation (vertical facing direction)
    float pitch;

    SpawnPoint(glm::vec3 pos = glm::vec3(0.0f), float y = 0.0f, float p = 0.0f)
        : position(pos), yaw(y), pitch(p) {}
};

/**
 * @brief Structure representing a bomb site location (for Bomb Defusal mode)
 */
struct BombSiteData {
    /// Position of the bomb site
    glm::vec3 position;
    /// Label: 'A' or 'B'
    char label;
    /// Whether this site is active
    bool active;

    BombSiteData(glm::vec3 pos = glm::vec3(0.0f), char lbl = 'A', bool act = true)
        : position(pos), label(lbl), active(act) {}
};

/**
 * @brief Structure representing a team spawn point (for Team vs Team)
 */
struct TeamSpawn {
    /// Position of the spawn point
    glm::vec3 position;
    /// Team ID (0 = Team A/Red, 1 = Team B/Blue)
    int teamId;
    /// Yaw rotation (horizontal facing direction)
    float yaw;
    /// Pitch rotation (vertical facing direction)
    float pitch;

    TeamSpawn(glm::vec3 pos = glm::vec3(0.0f), int team = 0, float y = 0.0f, float p = 0.0f)
        : position(pos), teamId(team), yaw(y), pitch(p) {}
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
     * @brief Delete a block at the specified position
     *
     * @param position Position to check for block deletion
     * @param tolerance Distance tolerance for matching block
     * @return true if a block was deleted, false otherwise
     */
    bool deleteBlockAt(const glm::vec3& position, float tolerance = 0.1f);
    
    /**
     * @brief Find and delete a block with exact position, scale, and orientation match
     *
     * @param position Position to match
     * @param scale Scale to match
     * @param rotation Rotation (yaw) to match
     * @param pitch Pitch to match
     * @param tolerance Distance tolerance for position matching
     * @return true if a block was deleted, false otherwise
     */
    bool deleteBlockExact(const glm::vec3& position, const glm::vec3& scale, float rotation, float pitch, float tolerance = 0.05f);

    /**
     * @brief Replace the texture of a block at the specified position
     *
     * @param position Position inside the target block
     * @param newTextureId New texture ID to assign
     * @return true if a block was found and updated, false otherwise
     */
    bool replaceTextureAt(const glm::vec3& position, int newTextureId);

    /**
     * @brief Find the index of the first block whose bounds (expanded by tolerance) contain position.
     *
     * @param position Point to test
     * @param tolerance Expansion applied to each block face
     * @return Index into getBlocks(), or -1 if none found
     */
    int findBlockAt(const glm::vec3& position, float tolerance = 0.15f) const;
    
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
     * @brief Check if a position collides with any solid block
     *
     * @param position Position to check
     * @param radius Collision radius
     * @return true if collision detected
     */
    bool checkCollision(const glm::vec3& position, float radius = 0.5f) const;

    /**
     * @brief Get the ground height and normal at a position (for walking on rotated blocks)
     *
     * @param position Position to check
     * @param radius Collision radius
     * @param groundHeight Output ground height
     * @param groundNormal Output ground normal
     * @return true if ground found
     */
    bool getGroundAt(const glm::vec3& position, float radius, float& groundHeight, glm::vec3& groundNormal) const;
    
    /**
     * @brief Get map dimensions
     *
     * @return glm::vec3 Map dimensions (width, height, depth)
     */
    glm::vec3 getDimensions() const;

    /**
     * @brief Set map dimensions
     *
     * @param dims New dimensions (width, height, depth)
     */
    void setDimensions(const glm::vec3& dims);

    /**
     * @brief Clear all blocks from the map
     */
    void clear();

    /**
     * @brief Add a spawn point (for Deathmatch mode)
     *
     * @param spawn SpawnPoint to add
     */
    void addSpawnPoint(const SpawnPoint& spawn);

    /**
     * @brief Remove a spawn point by index
     *
     * @param index Index of spawn point to remove
     */
    void removeSpawnPoint(size_t index);

    /**
     * @brief Add a team spawn point (for Team vs Team mode)
     *
     * @param spawn TeamSpawn to add
     */
    void addTeamSpawn(const TeamSpawn& spawn);

    /**
     * @brief Remove a team spawn point by index
     *
     * @param index Index of team spawn point to remove
     */
    void removeTeamSpawn(size_t index);

    /**
     * @brief Get all spawn points
     *
     * @return const std::vector<SpawnPoint>& Reference to spawn points vector
     */
    const std::vector<SpawnPoint>& getSpawnPoints() const;

    /**
     * @brief Get all team spawn points
     *
     * @return const std::vector<TeamSpawn>& Reference to team spawns vector
     */
    const std::vector<TeamSpawn>& getTeamSpawns() const;

    /**
     * @brief Get a random spawn point (for Deathmatch mode)
     *
     * @return SpawnPoint Random spawn point, or default if none exist
     */
    SpawnPoint getRandomSpawnPoint() const;

    /**
     * @brief Get a random spawn point for a specific team
     *
     * @param teamId Team ID (0 = Team A/Red, 1 = Team B/Blue)
     * @return TeamSpawn Random team spawn, or default if none exist for team
     */
    TeamSpawn getRandomTeamSpawn(int teamId) const;

    /**
     * @brief Add a bomb site location
     *
     * @param site BombSiteData to add
     */
    void addBombSite(const BombSiteData& site);

    /**
     * @brief Remove a bomb site by index
     *
     * @param index Index of bomb site to remove
     */
    void removeBombSite(size_t index);

    /**
     * @brief Get all bomb sites
     *
     * @return const std::vector<BombSiteData>& Reference to bomb sites vector
     */
    const std::vector<BombSiteData>& getBombSites() const;

private:
    /// All blocks in the map
    std::vector<MapBlock> blocks;
    /// Available textures
    std::vector<Texture*> textures;
    /// Map dimensions
    glm::vec3 dimensions;
    /// Map name
    std::string mapName;
    /// Spawn points for Deathmatch mode
    std::vector<SpawnPoint> spawnPoints;
    /// Team spawn points for Team vs Team mode
    std::vector<TeamSpawn> teamSpawns;
    /// Bomb sites for Bomb Defusal mode
    std::vector<BombSiteData> bombSites;
    
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
