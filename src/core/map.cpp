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
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
            float rotation = 0.0f;
            float pitch = 0.0f;

            // Try to parse with color (new format)
            lineStream >> x >> y >> z >> sx >> sy >> sz >> textureId >> blockType >> solid;

            // Check if color values are present
            if (lineStream >> r >> g >> b) {
                // Check if rotation and pitch are present (newest format)
                if (lineStream >> rotation >> pitch) {
                    // New format with color, rotation, and pitch
                    addBlock(MapBlock(glm::vec3(x, y, z), glm::vec3(sx, sy, sz),
                                     textureId, blockType, solid, glm::vec3(r, g, b),
                                     rotation, pitch));
                } else {
                    // New format with color only
                    addBlock(MapBlock(glm::vec3(x, y, z), glm::vec3(sx, sy, sz),
                                     textureId, blockType, solid, glm::vec3(r, g, b)));
                }
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
        else if (command == "spawn") {
            // Deathmatch spawn point: spawn x y z yaw pitch
            float x, y, z, yaw, pitch;
            lineStream >> x >> y >> z >> yaw >> pitch;
            addSpawnPoint(SpawnPoint(glm::vec3(x, y, z), yaw, pitch));
        }
        else if (command == "team_spawn") {
            // Team spawn point: team_spawn x y z teamId yaw pitch
            float x, y, z, yaw, pitch;
            int teamId;
            lineStream >> x >> y >> z >> teamId >> yaw >> pitch;
            addTeamSpawn(TeamSpawn(glm::vec3(x, y, z), teamId, yaw, pitch));
        }
        else if (command == "bomb_site") {
            // Bomb site: bomb_site x y z label
            float x, y, z;
            std::string labelStr;
            lineStream >> x >> y >> z >> labelStr;
            char label = 'A';
            if (!labelStr.empty()) label = toupper(labelStr[0]);
            if (label != 'A' && label != 'B') label = 'A';
            addBombSite(BombSiteData(glm::vec3(x, y, z), label));
        }
    }

    std::cout << "Loaded map: " << mapName << " with " << blocks.size() << " blocks, "
              << spawnPoints.size() << " DM spawns, " << teamSpawns.size() << " team spawns, "
              << bombSites.size() << " bomb sites" << std::endl;
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

int Map::findBlockAt(const glm::vec3& position, float tolerance) const {
    for (int i = 0; i < (int)blocks.size(); ++i) {
        glm::vec3 blockMin = blocks[i].position - blocks[i].scale * 0.5f - glm::vec3(tolerance);
        glm::vec3 blockMax = blocks[i].position + blocks[i].scale * 0.5f + glm::vec3(tolerance);
        if (position.x >= blockMin.x && position.x <= blockMax.x &&
            position.y >= blockMin.y && position.y <= blockMax.y &&
            position.z >= blockMin.z && position.z <= blockMax.z) {
            return i;
        }
    }
    return -1;
}

bool Map::deleteBlockAt(const glm::vec3& position, float tolerance) {
    int idx = findBlockAt(position, tolerance);
    if (idx >= 0) {
        blocks.erase(blocks.begin() + idx);
        return true;
    }
    return false;
}

bool Map::deleteBlockExact(const glm::vec3& position, const glm::vec3& scale, float rotation, float pitch, float tolerance) {
    for (int i = 0; i < (int)blocks.size(); ++i) {
        // Check position match (with tolerance)
        glm::vec3 posDiff = glm::abs(blocks[i].position - position);
        if (posDiff.x <= tolerance && posDiff.y <= tolerance && posDiff.z <= tolerance) {
            // Check scale match (with tolerance)
            glm::vec3 scaleDiff = glm::abs(blocks[i].scale - scale);
            if (scaleDiff.x <= tolerance && scaleDiff.y <= tolerance && scaleDiff.z <= tolerance) {
                // Check rotation match (with tolerance)
                float rotDiff = std::abs(blocks[i].rotation - rotation);
                float pitchDiff = std::abs(blocks[i].pitch - pitch);
                // Normalize rotation difference (handle 0/360 wraparound)
                if (rotDiff > 180.0f) rotDiff = 360.0f - rotDiff;

                if (rotDiff <= 5.0f && pitchDiff <= 5.0f) {
                    blocks.erase(blocks.begin() + i);
                    return true;
                }
            }
        }
    }
    return false;
}

bool Map::replaceTextureAt(const glm::vec3& position, int newTextureId) {
    int idx = findBlockAt(position, 0.15f);
    if (idx >= 0) {
        blocks[idx].textureId = newTextureId;
        return true;
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

        // Transform player position into block's local space to account for rotation
        glm::mat4 invRotation = glm::mat4(1.0f);
        invRotation = glm::rotate(invRotation, glm::radians(-block.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        invRotation = glm::rotate(invRotation, glm::radians(-block.rotation), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 localPos = glm::vec3(invRotation * glm::vec4(position - block.position, 1.0f)) + block.position;

        // AABB collision detection in local space
        glm::vec3 blockMin = block.position - block.scale * 0.5f;
        glm::vec3 blockMax = block.position + block.scale * 0.5f;
        glm::vec3 posMin = localPos - glm::vec3(radius);
        glm::vec3 posMax = localPos + glm::vec3(radius);

        if (posMin.x < blockMax.x && posMax.x > blockMin.x &&
            posMin.y < blockMax.y && posMax.y > blockMin.y &&
            posMin.z < blockMax.z && posMax.z > blockMin.z) {
            return true;
        }
    }
    return false;
}

bool Map::getGroundAt(const glm::vec3& position, float radius, float& groundHeight, glm::vec3& groundNormal) const {
    groundHeight = -1000.0f;
    groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    bool foundGround = false;

    for (const auto& block : blocks) {
        if (!block.solid) continue;

        // Transform player position into block's local space
        glm::mat4 invRotation = glm::mat4(1.0f);
        invRotation = glm::rotate(invRotation, glm::radians(-block.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        invRotation = glm::rotate(invRotation, glm::radians(-block.rotation), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 localPos = glm::vec3(invRotation * glm::vec4(position - block.position, 1.0f)) + block.position;

        // Block bounds in local space
        glm::vec3 blockMin = block.position - block.scale * 0.5f;
        glm::vec3 blockMax = block.position + block.scale * 0.5f;

        // Check if player is horizontally within the block (with radius)
        if (localPos.x + radius > blockMin.x && localPos.x - radius < blockMax.x &&
            localPos.z + radius > blockMin.z && localPos.z - radius < blockMax.z) {

            // Check if player is above the block. Strict: a surface whose top
            // is exactly at the feet must not count as being "below" us.
            if (localPos.y > blockMax.y + 0.001f) {
                // Calculate ground height in local space (top of block)
                float localGroundHeight = blockMax.y;

                // Transform ground height back to world space
                glm::mat4 rotation = glm::mat4(1.0f);
                rotation = glm::rotate(rotation, glm::radians(block.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
                rotation = glm::rotate(rotation, glm::radians(block.rotation), glm::vec3(0.0f, 1.0f, 0.0f));

                glm::vec3 localGroundPos = glm::vec3(localPos.x, localGroundHeight, localPos.z);
                glm::vec3 worldGroundPos = glm::vec3(rotation * glm::vec4(localGroundPos - block.position, 1.0f)) + block.position;

                // Transform the local up normal to world space
                glm::vec3 localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 worldNormal = glm::vec3(rotation * glm::vec4(localNormal, 0.0f));

                // Keep the highest ground
                if (worldGroundPos.y > groundHeight) {
                    groundHeight = worldGroundPos.y;
                    groundNormal = glm::normalize(worldNormal);
                    foundGround = true;
                }
            }
        }
    }

    return foundGround;
}

bool Map::getFloorBelow(const glm::vec3& position, float radius, float maxDrop,
                        float& groundHeight) const {
    float best = -1000000.0f;
    const float epsilon = 0.01f;   // surfaces this far above the feet still count

    for (const auto& block : blocks) {
        if (!block.solid) continue;

        glm::mat4 invRotation = glm::mat4(1.0f);
        invRotation = glm::rotate(invRotation, glm::radians(-block.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        invRotation = glm::rotate(invRotation, glm::radians(-block.rotation), glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::vec3 localPos = glm::vec3(invRotation * glm::vec4(position - block.position, 1.0f)) + block.position;
        const glm::vec3 blockMin = block.position - block.scale * 0.5f;
        const glm::vec3 blockMax = block.position + block.scale * 0.5f;

        // Horizontal overlap. The body reaches past the edge by its radius;
        // the epsilon keeps support at a feet point exactly on an edge (walking
        // to the very rim of a platform must not make the floor vanish).
        const float eps = 0.01f;
        if (localPos.x + radius <= blockMin.x - eps || localPos.x - radius >= blockMax.x + eps) continue;
        if (localPos.z + radius <= blockMin.z - eps || localPos.z - radius >= blockMax.z + eps) continue;

        // Only a surface we are on or above can hold us: its top must not be
        // above the feet (that block is a ceiling, not the floor).
        const float top = blockMax.y;
        if (top > position.y + epsilon) continue;
        if (position.y - top > maxDrop) continue;

        if (top > best) best = top;
    }

    if (best <= -999999.0f) {
        groundHeight = position.y;   // no floor below: caller keeps its height
        return false;
    }
    groundHeight = best;
    return true;
}

glm::vec3 Map::getDimensions() const {
    return dimensions;
}

void Map::clear() {
    blocks.clear();
    spawnPoints.clear();
    teamSpawns.clear();

    // Clean up textures
    for (auto* texture : textures) {
        if (texture != nullptr) {
            delete texture;
        }
    }
    textures.clear();
}

void Map::setDimensions(const glm::vec3& dims) {
    dimensions = dims;
}

void Map::addSpawnPoint(const SpawnPoint& spawn) {
    spawnPoints.push_back(spawn);
}

void Map::removeSpawnPoint(size_t index) {
    if (index < spawnPoints.size()) {
        spawnPoints.erase(spawnPoints.begin() + index);
    }
}

void Map::addTeamSpawn(const TeamSpawn& spawn) {
    teamSpawns.push_back(spawn);
}

void Map::removeTeamSpawn(size_t index) {
    if (index < teamSpawns.size()) {
        teamSpawns.erase(teamSpawns.begin() + index);
    }
}

const std::vector<SpawnPoint>& Map::getSpawnPoints() const {
    return spawnPoints;
}

const std::vector<TeamSpawn>& Map::getTeamSpawns() const {
    return teamSpawns;
}

SpawnPoint Map::getRandomSpawnPoint() const {
    if (spawnPoints.empty()) {
        return SpawnPoint(); // Return default spawn
    }
    int index = rand() % spawnPoints.size();
    return spawnPoints[index];
}

TeamSpawn Map::getRandomTeamSpawn(int teamId) const {
    std::vector<const TeamSpawn*> teamSpawnsForTeam;
    for (const auto& spawn : teamSpawns) {
        if (spawn.teamId == teamId) {
            teamSpawnsForTeam.push_back(&spawn);
        }
    }

    if (teamSpawnsForTeam.empty()) {
        return TeamSpawn(); // Return default spawn
    }

    int index = rand() % teamSpawnsForTeam.size();
    return *teamSpawnsForTeam[index];
}

void Map::addBombSite(const BombSiteData& site) {
    bombSites.push_back(site);
}

void Map::removeBombSite(size_t index) {
    if (index < bombSites.size()) {
        bombSites.erase(bombSites.begin() + index);
    }
}

const std::vector<BombSiteData>& Map::getBombSites() const {
    return bombSites;
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
