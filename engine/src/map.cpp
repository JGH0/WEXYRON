/**
 * @file map.cpp
 * @brief Implementation of Map class for managing 3D game maps
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "engine/map.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
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
    blocks.back().updateBoundsCache();
    rebuildCollisionGrid();
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
    rebuildCollisionGrid();
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

// ===========================================================================
// Rigid body collision (box vs blocks)
// ===========================================================================

void mapBlockBasis(const MapBlock& block, glm::vec3& axisX, glm::vec3& axisY, glm::vec3& axisZ) {
    // Same order and convention as the renderer (see rotationBasis there):
    // yaw around Y, then pitch around X, i.e. R = Ry * Rx.
    const float yaw = glm::radians(block.rotation);
    const float pit = glm::radians(block.pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pit), sp = std::sin(pit);

    axisX = glm::vec3(cy, 0.0f, -sy);
    axisY = glm::vec3(sy * sp, cp, cy * sp);
    axisZ = glm::vec3(sy * cp, -sp, cy * cp);
}

glm::vec3 mapBlockToLocalDir(const MapBlock& block, const glm::vec3& worldDir) {
    glm::vec3 ax, ay, az;
    mapBlockBasis(block, ax, ay, az);
    // The basis is orthonormal, so the inverse is the transpose.
    return glm::vec3(glm::dot(worldDir, ax), glm::dot(worldDir, ay), glm::dot(worldDir, az));
}

void MapBlock::updateBoundsCache() const {
    if (isAxisAligned()) {
        boundsMin_ = localMin();
        boundsMax_ = localMax();
        return;
    }
    glm::vec3 ax, ay, az;
    mapBlockBasis(*this, ax, ay, az);
    const glm::vec3 halfX = ax * (scale.x * 0.5f);
    const glm::vec3 halfY = ay * (scale.y * 0.5f);
    const glm::vec3 halfZ = az * (scale.z * 0.5f);
    const glm::vec3 extent(glm::abs(halfX.x) + glm::abs(halfY.x) + glm::abs(halfZ.x),
                           glm::abs(halfX.y) + glm::abs(halfY.y) + glm::abs(halfZ.y),
                           glm::abs(halfX.z) + glm::abs(halfY.z) + glm::abs(halfZ.z));
    boundsMin_ = position - extent;
    boundsMax_ = position + extent;
}

void MapBlock::worldBounds(glm::vec3& mn, glm::vec3& mx) const {
    mn = boundsMin_;
    mx = boundsMax_;
}

bool MapBlock::containsPoint(const glm::vec3& point) const {
    if (isAxisAligned()) {
        const glm::vec3 mn = localMin();
        const glm::vec3 mx = localMax();
        return point.x >= mn.x && point.x <= mx.x &&
               point.y >= mn.y && point.y <= mx.y &&
               point.z >= mn.z && point.z <= mx.z;
    }
    // Box local space: the point relative to the block centre, projected on
    // the block's own axes.
    const glm::vec3 d = point - position;
    glm::vec3 ax, ay, az;
    mapBlockBasis(*this, ax, ay, az);
    const glm::vec3 l(glm::dot(d, ax), glm::dot(d, ay), glm::dot(d, az));
    const glm::vec3 h = scale * 0.5f;
    return std::abs(l.x) <= h.x && std::abs(l.y) <= h.y && std::abs(l.z) <= h.z;
}

// ---------------------------------------------------------------------------
// Broad phase
// ---------------------------------------------------------------------------

void Map::rebuildCollisionGrid() {
    gridDirty_ = true;
}

void Map::ensureGrid() const {
    if (!gridDirty_) return;

    grid_.clear();
    gridDirty_ = false;

    for (size_t i = 0; i < blocks.size(); ++i) {
        const MapBlock& b = blocks[i];
        b.updateBoundsCache();   // mutable cache: const map, lazy fill
        if (!b.solid) continue;

        const glm::vec3& mn = b.boundsMin_;
        const glm::vec3& mx = b.boundsMax_;
        // One cell of padding so a query on a cell boundary still finds it.
        const int x0 = static_cast<int>(std::floor((mn.x - 0.01f) / kGridCell));
        const int x1 = static_cast<int>(std::floor((mx.x + 0.01f) / kGridCell));
        const int y0 = static_cast<int>(std::floor((mn.y - 0.01f) / kGridCell));
        const int y1 = static_cast<int>(std::floor((mx.y + 0.01f) / kGridCell));
        const int z0 = static_cast<int>(std::floor((mn.z - 0.01f) / kGridCell));
        const int z1 = static_cast<int>(std::floor((mx.z + 0.01f) / kGridCell));
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    GridCell cell;
                    cell.x = x; cell.y = y; cell.z = z;
                    grid_[cell].push_back(static_cast<int>(i));
                }
            }
        }
    }
}

template <typename Fn>
void Map::forEachBlockNear(const glm::vec3& mn, const glm::vec3& mx, Fn&& fn) const {
    ensureGrid();
    const int x0 = static_cast<int>(std::floor(mn.x / kGridCell));
    const int x1 = static_cast<int>(std::floor(mx.x / kGridCell));
    const int y0 = static_cast<int>(std::floor(mn.y / kGridCell));
    const int y1 = static_cast<int>(std::floor(mx.y / kGridCell));
    const int z0 = static_cast<int>(std::floor(mn.z / kGridCell));
    const int z1 = static_cast<int>(std::floor(mx.z / kGridCell));

    // A block can appear in several cells; visit it once per query.
    thread_local int stamp = 0;
    thread_local std::vector<int> visitStamp;

    GridCell cell;
    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                cell.x = x; cell.y = y; cell.z = z;
                auto it = grid_.find(cell);
                if (it == grid_.end()) continue;
                for (int index : it->second) {
                    if (visitStamp.size() <= static_cast<size_t>(index)) {
                        visitStamp.resize(static_cast<size_t>(index) + 64, -1);
                    }
                    if (visitStamp[static_cast<size_t>(index)] == stamp) continue;
                    visitStamp[static_cast<size_t>(index)] = stamp;
                    if (!fn(blocks[static_cast<size_t>(index)])) return;
                }
            }
        }
    }
    ++stamp;
}

// ---------------------------------------------------------------------------
// Narrow phase: axis-aligned body box vs block
// ---------------------------------------------------------------------------

namespace {

/// Separating-axis test of a world AABB (mn..mx) against one block, plus the
/// shortest way to push the box out. Returns false when they miss.
///
/// Axis-aligned blocks (every map block that is not tilted or turned) reduce to
/// a plain AABB overlap with the minimal translation axis. Rotated blocks use
/// the 15 axes a box-to-box test needs; that is still cheap, and only rotated
/// blocks go that way.
bool boxOverlapsBlockImpl(const MapBlock& block,
                          const glm::vec3& mn, const glm::vec3& mx,
                          glm::vec3& pushOut) {
    const glm::vec3 centre = (mn + mx) * 0.5f;
    const glm::vec3 half = (mx - mn) * 0.5f;

    if (block.isAxisAligned()) {
        const glm::vec3 bmn = block.localMin();
        const glm::vec3 bmx = block.localMax();
        const glm::vec3 d1 = mx - bmn;   // overlap on + axes
        const glm::vec3 d2 = bmx - mn;   // overlap on - axes
        if (d1.x <= 0.0f || d1.y <= 0.0f || d1.z <= 0.0f) return false;
        if (d2.x <= 0.0f || d2.y <= 0.0f || d2.z <= 0.0f) return false;

        // Move out along the axis with the smallest overlap.
        glm::vec3 out(0.0f);
        const float ox = std::min(d1.x, d2.x);
        const float oy = std::min(d1.y, d2.y);
        const float oz = std::min(d1.z, d2.z);
        if (ox <= oy && ox <= oz)      out.x = (d1.x < d2.x) ? -d1.x : d2.x;
        else if (oy <= oz)             out.y = (d1.y < d2.y) ? -d1.y : d2.y;
        else                           out.z = (d1.z < d2.z) ? -d1.z : d2.z;
        pushOut = out;
        return true;
    }

    // Oriented block: separating axes are the 3 box axes, the 3 block axes and
    // the 9 edge-pair cross products.
    glm::vec3 ax, ay, az;
    mapBlockBasis(block, ax, ay, az);
    const glm::vec3 bh = block.scale * 0.5f;
    const glm::vec3 d = centre - block.position;

    float bestOverlap = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);
    bool bestIsBoxAxis = false;
    glm::vec3 bestBoxAxis(0.0f);

    auto testAxis = [&](glm::vec3 axis, bool boxAxis) {
        const float len = glm::length(axis);
        if (len < 1e-6f) return true;            // degenerate: ignore
        axis /= len;

        const float ra = half.x * std::abs(axis.x) + half.y * std::abs(axis.y) + half.z * std::abs(axis.z);
        const float rb = bh.x * std::abs(glm::dot(axis, ax)) +
                         bh.y * std::abs(glm::dot(axis, ay)) +
                         bh.z * std::abs(glm::dot(axis, az));
        const float distance = glm::dot(d, axis);
        const float overlap = ra + rb - std::abs(distance);
        if (overlap <= 0.0f) return false;        // separated

        if (overlap < bestOverlap) {
            bestOverlap = overlap;
            bestAxis = (distance < 0.0f) ? -axis : axis;
            bestIsBoxAxis = boxAxis;
            bestBoxAxis = axis;
        }
        return true;
    };

    const glm::vec3 boxAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const glm::vec3 blockAxes[3] = {ax, ay, az};
    for (const glm::vec3& a : boxAxes) if (!testAxis(a, true)) return false;
    for (const glm::vec3& a : blockAxes) if (!testAxis(a, false)) return false;
    for (const glm::vec3& a : boxAxes) {
        for (const glm::vec3& b : blockAxes) {
            if (!testAxis(glm::cross(a, b), false)) return false;
        }
    }

    // Push out along the smallest axis, preferring a world axis when it is
    // almost as short (keeps sliding along floors and walls predictable).
    glm::vec3 out = bestAxis * (bestOverlap + 1e-4f);
    (void)bestIsBoxAxis;
    (void)bestBoxAxis;
    pushOut = out;
    return true;
}

} // namespace

bool Map::boxOverlapsBlock(const glm::vec3& mn, const glm::vec3& mx) const {
    glm::vec3 unused;
    return boxOverlapsBlock(mn, mx, unused);
}

bool Map::boxOverlapsBlock(const glm::vec3& mn, const glm::vec3& mx, glm::vec3& normal) const {
    bool hit = false;
    glm::vec3 best(0.0f);
    float bestLen = std::numeric_limits<float>::max();

    forEachBlockNear(mn, mx, [&](const MapBlock& b) {
        // Cheap AABB rejection before the exact test.
        glm::vec3 bmn, bmx;
        b.worldBounds(bmn, bmx);
        if (mx.x <= bmn.x || mn.x >= bmx.x) return true;
        if (mx.y <= bmn.y || mn.y >= bmx.y) return true;
        if (mx.z <= bmn.z || mn.z >= bmx.z) return true;

        glm::vec3 push(0.0f);
        if (!boxOverlapsBlockImpl(b, mn, mx, push)) return true;
        const float len = glm::length(push);
        if (len < bestLen) {
            bestLen = len;
            best = push;
            hit = true;
        }
        return true;   // keep looking for the smallest push
    });

    if (!hit) return false;
    normal = (glm::length(best) > 1e-6f) ? glm::normalize(best) : glm::vec3(0.0f, 1.0f, 0.0f);
    return true;
}

void Map::resolveBox(const glm::vec3& move, glm::vec3& mn, glm::vec3& mx) const {
    // Iterate a few times: pushing out of one block can touch another in a
    // corner. Four passes is enough for map geometry (blocks are convex and
    // axis-aligned, and the move never exceeds one frame's travel).
    for (int pass = 0; pass < 4; ++pass) {
        glm::vec3 push(0.0f);
        if (!boxOverlapsBlock(mn, mx, push)) break;

        // Only move along the requested axes (Y for a falling body, X and Z
        // for horizontal motion, all for a stuck body).
        glm::vec3 delta(0.0f);
        if (move.x != 0.0f) delta.x = push.x;
        if (move.y != 0.0f) delta.y = push.y;
        if (move.z != 0.0f) delta.z = push.z;
        // If the smallest push has no intended component, use the smallest one
        // that does; failing that, leave the box where it is.
        if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f) {
            delta = push;
        }
        mn += delta;
        mx += delta;
    }
}

bool Map::supportHeight(const glm::vec3& mn, const glm::vec3& mx, float maxDrop,
                        float& height) const {
    const glm::vec3 footMin(mn.x, mn.y - maxDrop, mn.z);
    const glm::vec3 footMax(mx.x, mn.y + 0.02f, mx.z);   // include floats at the feet

    // Surfaces whose top is under the feet. Take the highest such surface whose
    // top is not covered by another block that starts right on it.
    std::vector<std::pair<float, const MapBlock*>> tops;
    forEachBlockNear(footMin, footMax, [&](const MapBlock& b) {
        glm::vec3 bmn, bmx;
        b.worldBounds(bmn, bmx);
        if (bmx.x <= mn.x || bmn.x >= mx.x) return true;
        if (bmx.z <= mn.z || bmn.z >= mx.z) return true;
        if (bmx.y > mn.y + 0.02f) return true;      // above the feet: a ceiling
        if (highestPoint(b) < mn.y - maxDrop) return true;
        tops.push_back({bmx.y, &b});
        return true;
    });
    if (tops.empty()) return false;

    std::sort(tops.begin(), tops.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [top, block] : tops) {
        bool covered = false;
        forEachBlockNear(glm::vec3(mn.x, top - 0.02f, mn.z), glm::vec3(mx.x, top + 0.05f, mx.z),
                         [&](const MapBlock& other) {
            if (&other == block) return true;
            glm::vec3 omn, omx;
            other.worldBounds(omn, omx);
            if (omx.x <= mn.x || omn.x >= mx.x) return true;
            if (omx.z <= mn.z || omn.z >= mx.z) return true;
            if (omn.y >= top - 0.02f && omn.y <= top + 0.05f) covered = true;
            return !covered;
        });
        if (!covered) {
            height = top;
            return true;
        }
    }
    height = tops.front().first;
    return true;
}

float Map::ceilingHeight(const glm::vec3& mn, const glm::vec3& mx, float maxRise) const {
    const glm::vec3 headMin(mn.x, mx.y, mn.z);
    const glm::vec3 headMax(mx.x, mx.y + std::max(0.0f, maxRise), mx.z);
    float lowest = headMax.y;
    forEachBlockNear(headMin, headMax, [&](const MapBlock& b) {
        glm::vec3 bmn, bmx;
        b.worldBounds(bmn, bmx);
        if (bmx.x <= mn.x || bmn.x >= mx.x) return true;
        if (bmx.z <= mn.z || bmn.z >= mx.z) return true;
        if (bmn.y < mx.y) return true;               // not above the head
        if (bmn.y < lowest) lowest = bmn.y;
        return true;
    });
    return lowest;
}

float Map::highestPoint(const MapBlock& block) const {
    glm::vec3 bmn, bmx;
    block.worldBounds(bmn, bmx);
    return bmx.y;
}

glm::vec3 Map::topNormalOf(std::size_t blockIndex) const {
    if (blockIndex >= blocks.size()) return glm::vec3(0.0f, 1.0f, 0.0f);
    const MapBlock& b = blocks[blockIndex];
    if (b.isAxisAligned()) return glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 ax, ay, az;
    mapBlockBasis(b, ax, ay, az);
    return ay;
}
