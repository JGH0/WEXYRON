/**
 * @file bot.cpp
 * @brief Portable bot AI (see bot.hpp)
 */

#include "bot.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <utility>

namespace bots {
namespace {

constexpr float kBodyHalfWidth = 0.4f;
constexpr float kBodyHeight = 1.8f;
constexpr float kEyeHeight = 1.6f;
constexpr float kBodyCentre = kBodyHeight * 0.5f;
constexpr float kGravity = 9.8f;
constexpr float kStepUp = 0.55f;
constexpr float kGroundSnap = 0.60f;
constexpr float kFlashRadius = 12.0f;
constexpr float kSightRange = 55.0f;

// Navigation grid.
constexpr float kNavCell = 1.0f;
constexpr float kNavStepUp = 0.55f;
constexpr float kNavMaxDrop = 2.40f;

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

/// Wrap an angle to (-180, 180].
float wrapDegrees(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees <= -180.0f) degrees += 360.0f;
    return degrees;
}

glm::vec3 forwardFromAngles(float yawDegrees, float pitchDegrees) {
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);
    return glm::vec3(std::cos(yaw) * std::cos(pitch),
                     std::sin(pitch),
                     std::sin(yaw) * std::cos(pitch));
}

glm::vec3 horizontalNormalized(const glm::vec3& v) {
    glm::vec3 flat(v.x, 0.0f, v.z);
    const float len = glm::length(flat);
    if (len < 0.0001f) return glm::vec3(0.0f);
    return flat / len;
}

} // namespace

// ===========================================================================
// BotMap
// ===========================================================================

bool BotMap::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    blocks.clear();
    spawns.clear();
    teamSpawns.clear();
    bombSites.clear();
    name = path;
    dimensions = glm::vec3(0.0f);

    std::string line;
    while (std::getline(in, line)) {
        // Comments and blank lines.
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string command;
        if (!(ls >> command)) continue;

        if (command == "block") {
            float x, y, z, sx, sy, sz;
            int textureId, blockType;
            int solid = 1;
            if (!(ls >> x >> y >> z >> sx >> sy >> sz >> textureId >> blockType)) continue;
            if (!(ls >> solid)) solid = 1;

            // A block can carry colour and optional yaw/pitch. The AI only
            // needs collision; rotated blocks are expanded conservatively.
            // Old-format maps omit the colour: in that format the value read
            // after blockType is not a collision flag (Map::parseMapData
            // forces every such block solid), so do the same here.
            float r, g, b, rotation = 0.0f, pitch = 0.0f;
            const bool hasColour = static_cast<bool>(ls >> r >> g >> b);
            if (hasColour) {
                if (!(ls >> rotation >> pitch)) { rotation = 0.0f; pitch = 0.0f; }
                if (solid == 0) continue;   // new format: explicit non-solid
            } else {
                solid = 1;                  // old format: always solid
            }

            const glm::vec3 centre(x, y, z);
            glm::vec3 half(std::abs(sx) * 0.5f, std::abs(sy) * 0.5f, std::abs(sz) * 0.5f);
            if (rotation != 0.0f || pitch != 0.0f) {
                // Conservative horizontal expansion (the authored maps are
                // axis-aligned; this only matters if such a block is added).
                half.x *= 1.5f;
                half.z *= 1.5f;
                half.y *= 1.2f;
            }
            blocks.push_back({centre - half, centre + half});
        } else if (command == "dimensions") {
            float w, h, d;
            if (ls >> w >> h >> d) dimensions = glm::vec3(std::abs(w), std::abs(h), std::abs(d));
        } else if (command == "name") {
            std::string mapName;
            if (ls >> mapName) name = mapName;
        } else if (command == "spawn") {
            float x, y, z, yaw = 0.0f, pitch = 0.0f;
            if (ls >> x >> y >> z) {
                ls >> yaw >> pitch;
                spawns.push_back({glm::vec3(x, y, z), yaw, pitch});
            }
        } else if (command == "team_spawn") {
            float x, y, z, yaw = 0.0f, pitch = 0.0f;
            int team = 0;
            if (ls >> x >> y >> z >> team) {
                ls >> yaw >> pitch;
                teamSpawns.push_back({glm::vec3(x, y, z), team, yaw, pitch});
            }
        } else if (command == "bomb_site") {
            float x, y, z;
            std::string label;
            if (ls >> x >> y >> z) {
                ls >> label;
                const char site = (!label.empty() && std::toupper(static_cast<unsigned char>(label[0])) == 'B') ? 'B' : 'A';
                bombSites.push_back({glm::vec3(x, y, z), site});
            }
        }
    }

    if (dimensions.x <= 0.0f || dimensions.y <= 0.0f || dimensions.z <= 0.0f) {
        // Infer a sensible extent from the blocks (maps usually define it).
        glm::vec3 lo(0.0f), hi(0.0f);
        bool first = true;
        for (const Block& block : blocks) {
            if (first) { lo = block.min; hi = block.max; first = false; }
            lo = glm::min(lo, block.min);
            hi = glm::max(hi, block.max);
        }
        dimensions = first ? glm::vec3(20.0f, 10.0f, 20.0f)
                           : glm::vec3(std::max(20.0f, (hi.x - lo.x) * 1.2f),
                                       std::max(10.0f, (hi.y - lo.y) * 1.5f),
                                       std::max(20.0f, (hi.z - lo.z) * 1.2f));
    }

    std::cout << "[Bots] Map " << name << ": " << blocks.size() << " solid blocks, "
              << spawns.size() << " spawns, " << teamSpawns.size() << " team spawns, "
              << bombSites.size() << " bomb sites" << std::endl;

    buildNavigation();
    return true;
}

BotMap BotMap::fallback(const std::string& mapName) {
    BotMap map;
    map.name = mapName;
    map.dimensions = glm::vec3(20.0f, 10.0f, 20.0f);
    // A single large floor at y = 0. Blocks are 1 unit thick below the surface.
    map.blocks.push_back({glm::vec3(-20.0f, -1.0f, -20.0f), glm::vec3(20.0f, 0.0f, 20.0f)});
    map.buildNavigation();
    return map;
}

bool BotMap::boxBlocked(const glm::vec3& min, const glm::vec3& max) const {
    constexpr float eps = 0.0001f;
    for (const Block& block : blocks) {
        if (max.x <= block.min.x + eps || min.x >= block.max.x - eps) continue;
        if (max.y <= block.min.y + eps || min.y >= block.max.y - eps) continue;
        if (max.z <= block.min.z + eps || min.z >= block.max.z - eps) continue;
        return true;
    }
    return false;
}

bool BotMap::bodyBlocked(const glm::vec3& feet) const {
    const glm::vec3 mn(feet.x - kBodyHalfWidth, feet.y, feet.z - kBodyHalfWidth);
    const glm::vec3 mx(feet.x + kBodyHalfWidth, feet.y + kBodyHeight, feet.z + kBodyHalfWidth);
    return boxBlocked(mn, mx);
}

bool BotMap::lineBlocked(const glm::vec3& a, const glm::vec3& b) const {
    const glm::vec3 dir = b - a;
    const float length = glm::length(dir);
    if (length < 0.0001f) return false;
    const glm::vec3 d = dir / length;

    for (const Block& block : blocks) {
        float tmin = 0.0f;
        float tmax = length;
        bool hit = true;

        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(d[axis]) < 0.00001f) {
                if (a[axis] < block.min[axis] || a[axis] > block.max[axis]) { hit = false; break; }
                continue;
            }
            float t1 = (block.min[axis] - a[axis]) / d[axis];
            float t2 = (block.max[axis] - a[axis]) / d[axis];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) { hit = false; break; }
        }

        // Ignore a wall that starts at the ray origin (the observer's own
        // block) and only block when the hit lies inside the segment.
        if (hit && tmin <= length && tmax > 0.01f) return true;
    }
    return false;
}

bool BotMap::topUnder(float x, float z, float fromY, float& y) const {
    bool found = false;
    float best = -1e9f;
    for (const Block& block : blocks) {
        if (x < block.min.x || x > block.max.x) continue;
        if (z < block.min.z || z > block.max.z) continue;
        if (block.max.y > fromY + 0.001f) continue;
        if (block.max.y > best) { best = block.max.y; found = true; }
    }
    if (found) y = best;
    return found;
}

bool BotMap::groundBelow(const glm::vec3& feet, float radius, float maxDrop, float& height) const {
    bool found = false;
    float best = -1e9f;
    for (const Block& block : blocks) {
        if (feet.x + radius < block.min.x || feet.x - radius > block.max.x) continue;
        if (feet.z + radius < block.min.z || feet.z - radius > block.max.z) continue;
        if (block.max.y > feet.y + 0.02f) continue;
        if (block.max.y < feet.y - maxDrop) continue;
        if (block.max.y > best) { best = block.max.y; found = true; }
    }
    if (found) height = best;
    return found;
}

bool BotMap::standable(const glm::vec3& feet) const {
    if (bodyBlocked(feet)) return false;
    // An empty world has a virtual floor at y = 0. A map with geometry does
    // not: a point with no supporting block underneath is a hole, even if it
    // happens to be at y = 0.
    if (blocks.empty()) return feet.y <= 0.05f;
    float ground = 0.0f;
    return groundBelow(feet, kBodyHalfWidth, kGroundSnap, ground);
}

bool BotMap::moveBlocked(const glm::vec3& a, const glm::vec3& b) const {
    const glm::vec3 delta = b - a;
    const float distance = glm::length(delta);
    if (distance < 0.0001f) return false;

    // A step up/down is allowed: sample the body at the higher of the two feet,
    // so standing on the destination step is clear while a wall is not.
    const float sampleY = std::max(a.y, b.y);
    const int samples = std::max(2, static_cast<int>(std::ceil(distance / 0.35f)));
    for (int i = 1; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        const glm::vec3 p = a + delta * t;
        if (bodyBlocked(glm::vec3(p.x, sampleY, p.z))) return true;
    }
    return false;
}

void BotMap::buildNavigation() {
    navNodes_.clear();
    navReady_ = false;
    mainComponent_ = -1;
    if (blocks.empty()) {
        navReady_ = true;
        return;
    }

    const int countX = std::max(1, static_cast<int>(std::ceil(dimensions.x / kNavCell)));
    const int countZ = std::max(1, static_cast<int>(std::ceil(dimensions.z / kNavCell)));
    const float cellX = dimensions.x / static_cast<float>(countX);
    const float cellZ = dimensions.z / static_cast<float>(countZ);
    const float originX = -dimensions.x * 0.5f;
    const float originZ = -dimensions.z * 0.5f;
    if (cellX <= 0.0001f || cellZ <= 0.0001f) {
        navReady_ = true;
        return;
    }

    // Candidate standing heights are block top surfaces. Collecting them per
    // grid cell is much cheaper than testing every cell against every block.
    std::map<std::pair<int, int>, std::vector<float>> heights;
    for (const Block& block : blocks) {
        const float top = block.max.y;
        if (top < -20.0f || top > 90.0f) continue;

        int startX = static_cast<int>(std::ceil((block.min.x - originX) / cellX - 0.5f));
        int endX = static_cast<int>(std::floor((block.max.x - originX) / cellX - 0.5f));
        int startZ = static_cast<int>(std::ceil((block.min.z - originZ) / cellZ - 0.5f));
        int endZ = static_cast<int>(std::floor((block.max.z - originZ) / cellZ - 0.5f));
        startX = std::max(0, startX); endX = std::min(countX - 1, endX);
        startZ = std::max(0, startZ); endZ = std::min(countZ - 1, endZ);
        if (startX > endX || startZ > endZ) continue;

        for (int ix = startX; ix <= endX; ++ix) {
            for (int iz = startZ; iz <= endZ; ++iz) {
                heights[{ix, iz}].push_back(top);
            }
        }
    }

    std::map<std::pair<int, int>, std::vector<int>> cellNodes;
    std::vector<std::pair<int, int>> nodeCells;
    for (auto& entry : heights) {
        const int ix = entry.first.first;
        const int iz = entry.first.second;
        const float x = originX + (static_cast<float>(ix) + 0.5f) * cellX;
        const float z = originZ + (static_cast<float>(iz) + 0.5f) * cellZ;

        std::vector<float>& levels = entry.second;
        std::sort(levels.begin(), levels.end());
        float last = -1e9f;
        for (float y : levels) {
            if (y - last < 0.05f) continue;
            last = y;
            const glm::vec3 feet(x, y, z);
            if (!standable(feet)) continue;
            const int index = static_cast<int>(navNodes_.size());
            NavNode node;
            node.feet = feet;
            navNodes_.push_back(std::move(node));
            cellNodes[{ix, iz}].push_back(index);
            nodeCells.push_back({ix, iz});
        }
    }

    // Connect 8-neighbours: small steps up/down are walkable; larger drops are
    // one-way edges (the body can fall down but cannot climb back).
    for (size_t i = 0; i < navNodes_.size(); ++i) {
        const std::pair<int, int> cell = nodeCells[i];
        NavNode& node = navNodes_[i];
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                auto found = cellNodes.find({cell.first + dx, cell.second + dz});
                if (found == cellNodes.end()) continue;

                for (int otherIndex : found->second) {
                    if (otherIndex == static_cast<int>(i)) continue;
                    const NavNode& other = navNodes_[otherIndex];
                    const float dy = other.feet.y - node.feet.y;
                    if (dy > kNavStepUp || dy < -kNavMaxDrop) continue;
                    if (moveBlocked(node.feet, other.feet)) continue;

                    // Avoid duplicate links (two heights can share a cell).
                    bool duplicate = false;
                    for (int existing : node.neighbors) {
                        if (existing == otherIndex) { duplicate = true; break; }
                    }
                    if (!duplicate) node.neighbors.push_back(otherIndex);
                }
            }
        }
    }

    // Connected components: spawns/wandering only use the largest walkable
    // area, so a bot never starts on a random wall/roof island.
    std::vector<int> componentSize;
    for (size_t i = 0; i < navNodes_.size(); ++i) {
        if (navNodes_[i].component != -1) continue;
        const int component = static_cast<int>(componentSize.size());
        componentSize.push_back(0);
        std::queue<int> queue;
        queue.push(static_cast<int>(i));
        navNodes_[i].component = component;
        while (!queue.empty()) {
            const int at = queue.front();
            queue.pop();
            ++componentSize.back();
            for (int next : navNodes_[at].neighbors) {
                if (navNodes_[next].component != -1) continue;
                navNodes_[next].component = component;
                queue.push(next);
            }
        }
    }

    int bestSize = 0;
    for (size_t i = 0; i < componentSize.size(); ++i) {
        if (componentSize[i] > bestSize) {
            bestSize = componentSize[i];
            mainComponent_ = static_cast<int>(i);
        }
    }

    // Prefer the main *ground* area over a large roof/wall-top island. A wall
    // perimeter can contain more nodes than a small playable floor, but the
    // playable area has nodes near the map's lowest walkable level.
    float lowest = std::numeric_limits<float>::max();
    for (const NavNode& node : navNodes_) lowest = std::min(lowest, node.feet.y);
    std::vector<int> lowCounts(componentSize.size(), 0);
    for (const NavNode& node : navNodes_) {
        if (node.component < 0) continue;
        if (node.feet.y <= lowest + 2.0f) lowCounts[node.component]++;
    }
    int bestLow = -1;
    int bestLowComponent = mainComponent_;
    for (size_t i = 0; i < lowCounts.size(); ++i) {
        if (lowCounts[i] > bestLow ||
            (lowCounts[i] == bestLow && static_cast<int>(i) == mainComponent_)) {
            bestLow = lowCounts[i];
            bestLowComponent = static_cast<int>(i);
        }
    }
    if (bestLow > 0) mainComponent_ = bestLowComponent;

    size_t links = 0;
    for (const NavNode& node : navNodes_) links += node.neighbors.size();
    std::cout << "[Bots]   nav: " << navNodes_.size() << " nodes, " << links
              << " links, " << componentSize.size() << " area(s)" << std::endl;
    navReady_ = true;
}

int BotMap::nearestNode(const glm::vec3& feet, float maxDistance) const {
    int best = -1;
    float bestScore = std::numeric_limits<float>::max();
    for (size_t i = 0; i < navNodes_.size(); ++i) {
        const NavNode& node = navNodes_[i];
        if (node.neighbors.empty()) continue;
        const float dx = node.feet.x - feet.x;
        const float dz = node.feet.z - feet.z;
        const float horizontal = std::sqrt(dx * dx + dz * dz);
        const float vertical = std::abs(node.feet.y - feet.y);
        if (horizontal > maxDistance || vertical > 3.0f) continue;
        const float score = horizontal + vertical * 1.5f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool BotMap::findPath(const glm::vec3& from, const glm::vec3& to,
                      std::vector<glm::vec3>& path) const {
    path.clear();
    if (!navReady_ || navNodes_.empty()) return false;

    const int start = nearestNode(from, 6.0f);
    const int goal = nearestNode(to, 6.0f);
#ifdef BOT_NAV_DEBUG
    std::cout << "[NavDebug] start=" << start
              << (start >= 0 ? " c=" + std::to_string(navNodes_[start].component) : "")
              << " goal=" << goal
              << (goal >= 0 ? " c=" + std::to_string(navNodes_[goal].component) : "")
              << std::endl;
#endif
    if (start < 0 || goal < 0) return false;
    if (start == goal) {
        path.push_back(navNodes_[goal].feet);
        return true;
    }

    const size_t count = navNodes_.size();
    std::vector<float> gScore(count, std::numeric_limits<float>::max());
    std::vector<int> cameFrom(count, -1);
    std::vector<char> closed(count, 0);

    auto heuristic = [&](int index) {
        return glm::length(navNodes_[index].feet - navNodes_[goal].feet);
    };
    using QueueItem = std::pair<float, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> open;

    gScore[start] = 0.0f;
    open.push({heuristic(start), start});

    bool found = false;
    int iterations = 0;
    while (!open.empty() && iterations++ < 30000) {
        const int current = open.top().second;
        open.pop();
        if (closed[current]) continue;
        if (current == goal) { found = true; break; }
        closed[current] = 1;

        for (int next : navNodes_[current].neighbors) {
            if (closed[next]) continue;
            const float step = glm::length(navNodes_[next].feet - navNodes_[current].feet);
            const float tentative = gScore[current] + step;
            if (tentative >= gScore[next]) continue;
            gScore[next] = tentative;
            cameFrom[next] = current;
            open.push({tentative + heuristic(next), next});
        }
    }

    if (!found) return false;

    std::vector<int> reversed;
    for (int at = goal; at != -1; at = cameFrom[at]) {
        reversed.push_back(at);
        if (at == start) break;
    }
    std::reverse(reversed.begin(), reversed.end());
    if (reversed.empty() || reversed.front() != start) return false;

    for (size_t i = 1; i < reversed.size(); ++i) {
        path.push_back(navNodes_[reversed[i]].feet);
    }

    // Finish on the exact requested point when it is a valid standing spot.
    if (standable(to)) {
        const glm::vec3 tail = path.empty() ? navNodes_[start].feet : path.back();
        if (glm::length(to - tail) > 0.1f && !moveBlocked(tail, to)) {
            path.push_back(to);
        }
    }
    return !path.empty();
}

bool BotMap::randomNavPoint(std::mt19937& rng, glm::vec3& point) const {
    if (!navReady_ || navNodes_.empty()) return false;

    std::vector<int> candidates;
    for (size_t i = 0; i < navNodes_.size(); ++i) {
        if (navNodes_[i].neighbors.empty()) continue;
        if (mainComponent_ >= 0 && navNodes_[i].component != mainComponent_) continue;
        candidates.push_back(static_cast<int>(i));
    }
    if (candidates.empty()) {
        for (size_t i = 0; i < navNodes_.size(); ++i) {
            if (!navNodes_[i].neighbors.empty()) candidates.push_back(static_cast<int>(i));
        }
    }
    if (candidates.empty()) return false;

    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    point = navNodes_[candidates[pick(rng)]].feet;
    return true;
}

SpawnResult BotMap::findSpawn(bool teamMode, int team, std::mt19937& rng) const {
    // 1) Authored spawn points (preferred).
    std::vector<SpawnResult> candidates;
    if (teamMode) {
        for (const TeamSpawnPoint& spawn : teamSpawns) {
            if (spawn.team != team) continue;
            candidates.push_back({spawn.position, spawn.yaw});
        }
        if (candidates.empty()) {
            for (const TeamSpawnPoint& spawn : teamSpawns) candidates.push_back({spawn.position, spawn.yaw});
        }
    } else {
        for (const SpawnPoint& spawn : spawns) candidates.push_back({spawn.position, spawn.yaw});
    }

    if (!candidates.empty()) {
        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        const size_t start = pick(rng);
        for (size_t i = 0; i < candidates.size(); ++i) {
            const SpawnResult& candidate = candidates[(start + i) % candidates.size()];
            glm::vec3 feet = candidate.position;
            float ground = 0.0f;
            if (groundBelow(feet, kBodyHalfWidth, 2.0f, ground)) feet.y = ground;
            if (standable(feet)) return {feet, candidate.yaw};
        }
    }

    // 2) Navigation nodes: guaranteed walkable, on top of real ground and in
    // the main connected area. This is the safe fallback for maps without
    // authored spawns (and keeps bots off random wall/roof islands).
    if (!navNodes_.empty()) {
        std::uniform_int_distribution<size_t> pick(0, navNodes_.size() - 1);
        for (int attempt = 0; attempt < 120; ++attempt) {
            const size_t index = pick(rng);
            const NavNode& node = navNodes_[index];
            if (node.neighbors.empty()) continue;
            if (mainComponent_ >= 0 && node.component != mainComponent_) continue;
            if (!standable(node.feet)) continue;

            if (teamMode) {
                // Spawn teams on opposite halves when no authored team spawn
                // points exist.
                if (team == 0 && node.feet.x > dimensions.x * 0.15f) continue;
                if (team == 1 && node.feet.x < -dimensions.x * 0.15f) continue;
            }
            const float yaw = teamMode ? (team == 1 ? -90.0f : 90.0f) : 0.0f;
            return {node.feet, yaw};
        }
    }

    // 3) Ring search around the centre (last resort).
    const float halfX = std::max(1.0f, dimensions.x * 0.5f - 1.0f);
    const float halfZ = std::max(1.0f, dimensions.z * 0.5f - 1.0f);
    const float teamBias = teamMode ? (team == 1 ? dimensions.x * 0.22f : -dimensions.x * 0.22f) : 0.0f;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    auto testXZ = [&](float x, float z) -> bool {
        x = clampf(x, -halfX, halfX);
        z = clampf(z, -halfZ, halfZ);
        float top = 0.0f;
        if (!topUnder(x, z, dimensions.y + 5.0f, top)) top = 0.0f;
        const glm::vec3 feet(x, top, z);
        if (!standable(feet)) return false;
        candidates.push_back({feet, 0.0f});
        return true;
    };

    // A jittered first stop spreads a group of bots around instead of
    // spawning every one of them on the exact same centre point.
    {
        const float firstX = teamBias + (unit(rng) * 2.0f - 1.0f) * halfX * (teamMode ? 0.30f : 0.45f);
        const float firstZ = (unit(rng) * 2.0f - 1.0f) * halfZ * (teamMode ? 0.30f : 0.45f);
        if (testXZ(firstX, firstZ)) {
            SpawnResult& spawn = candidates.back();
            spawn.yaw = teamMode ? (team == 1 ? -90.0f : 90.0f) : 0.0f;
            return spawn;
        }
    }

    const float maxRadius = std::max(halfX, halfZ);
    const float angleOffset = unit(rng) * 6.28318f;
    for (float radius = 2.0f; radius <= maxRadius; radius += 2.0f) {
        constexpr int kAngles = 16;
        for (int i = 0; i < kAngles; ++i) {
            const float angle = angleOffset + (2.0f * 3.14159265f * static_cast<float>(i)) / kAngles;
            if (testXZ(teamBias + std::cos(angle) * radius, std::sin(angle) * radius)) {
                SpawnResult& spawn = candidates.back();
                spawn.yaw = glm::degrees(std::atan2(-spawn.position.z, -spawn.position.x));
                return spawn;
            }
        }
    }

    return {glm::vec3(0.0f, 0.0f, 0.0f), 0.0f};
}

// ===========================================================================
// Bot
// ===========================================================================

Bot::Bot(uint32_t id, uint8_t team, float accuracy, uint32_t randomSeed)
    : id_(id), team_(team), accuracy_(clampf(accuracy, 0.0f, 1.0f)), rng_(randomSeed) {
    grenadeTimer_ = randomRange(6.0f, 14.0f);
    flashTimer_ = randomRange(9.0f, 18.0f);
    fireTimer_ = randomRange(0.2f, 0.8f);
    strafeDir_ = (random01() < 0.5f) ? -1 : 1;
}

void Bot::teleport(const glm::vec3& feet, float yaw, float pitch, const BotMap& map) {
    position_ = feet;
    velocity_ = glm::vec3(0.0f);
    yaw_ = yaw;
    pitch_ = pitch;
    grounded_ = false;
    hasObjective_ = false;
    wanderTimer_ = 0.0f;
    stuckTimer_ = 0.0f;
    path_.clear();
    pathIndex_ = 0;
    pathGoal_ = position_;
    pathTimer_ = 0.0f;
    // Nudge out of a block if the spawn point is slightly embedded.
    if (map.bodyBlocked(position_)) {
        float ground = 0.0f;
        if (map.groundBelow(position_, kBodyHalfWidth, 2.0f, ground)) position_.y = ground;
    }
}

void Bot::markDead(float delay) {
    if (!alive_) return;
    alive_ = false;
    health_ = 0.0f;
    velocity_ = glm::vec3(0.0f);
    respawnTimer_ = std::max(0.1f, delay);
}

void Bot::respawn(const glm::vec3& spawn, float yaw, const BotMap& map) {
    alive_ = true;
    health_ = 100.0f;
    respawnTimer_ = 0.0f;
    blindTimer_ = 0.0f;
    reactionTimer_ = 0.0f;
    fireTimer_ = randomRange(0.2f, 0.9f);
    grenadeTimer_ = randomRange(5.0f, 12.0f);
    flashTimer_ = randomRange(8.0f, 16.0f);
    grenadeAmmo_ = 2;
    flashAmmo_ = 2;
    hasObjective_ = false;
    lastSeenTimer_ = 0.0f;
    lastTargetId_ = 0;
    teleport(spawn, yaw, 0.0f, map);
}

void Bot::applyFlash(const glm::vec3& origin, const BotMap& map) {
    if (!alive_) return;
    const glm::vec3 eye = position_ + glm::vec3(0.0f, kEyeHeight, 0.0f);
    const glm::vec3 toFlash = origin - eye;
    const float distance = glm::length(toFlash);
    if (distance > kFlashRadius || distance < 0.0001f) return;
    if (map.lineBlocked(eye, origin)) return;

    const glm::vec3 dir = toFlash / distance;
    const float facing = glm::dot(forwardFromAngles(yaw_, pitch_), dir);
    const float intensity = clampf(1.0f - distance / kFlashRadius, 0.0f, 1.0f);

    float duration;
    if (facing > 0.3f) {
        duration = (5.0f * intensity) + (3.0f * (1.0f - intensity));
        duration *= (0.5f + facing * 0.5f);
    } else if (facing > 0.0f) {
        duration = (3.0f * intensity) + (1.5f * (1.0f - intensity));
    } else {
        duration = 0.5f * intensity;
    }
    duration = std::max(duration, 0.25f);
    blindTimer_ = std::max(blindTimer_, duration);
}

float Bot::random01() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_);
}

float Bot::randomRange(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}

void Bot::idle(float dt, const BotMap* map) {
    if (!alive_) return;

    // Cancel horizontal movement / pathing and let gravity settle the body
    // onto the floor if it was in the air when the last player disconnected.
    velocity_.x = 0.0f;
    velocity_.z = 0.0f;
    clearPath();
    hasObjective_ = false;
    objectiveTimer_ = 0.0f;
    wanderTimer_ = 0.0f;
    stuckTimer_ = 0.0f;
    lastTargetId_ = 0;
    lastSeenTimer_ = 0.0f;

    updateVertical(clampf(dt, 0.001f, 0.1f), map);
}

const Target* Bot::selectTarget(const World& world) const {
    const Target* best = nullptr;
    float bestDistance = kSightRange;
    const glm::vec3 eye = position_ + glm::vec3(0.0f, kEyeHeight, 0.0f);

    for (const Target& target : world.targets) {
        if (!target.alive || target.id == id_) continue;
        if (world.gameMode >= 1 && !world.friendlyFire && target.team == team_) continue;
        if (target.isBot && target.team == team_ && world.gameMode == 0) {
            // Deathmatch: bots fight everyone, including other bots.
        }

        const float distance = glm::length(target.feet - position_);
        if (distance >= bestDistance) continue;

        const glm::vec3 aim = target.feet + glm::vec3(0.0f, kBodyCentre, 0.0f);
        if (world.map && world.map->lineBlocked(eye, aim)) continue;

        bestDistance = distance;
        best = &target;
    }
    return best;
}

glm::vec3 Bot::chooseObjective(const World& world) {
    const BotMap* map = world.map;
    const bool bombMode = world.gameMode == 2;

    if (bombMode) {
        // Bomb planted: both teams converge on it. Defenders defuse (the
        // server notices a defender standing at the bomb); attackers guard it.
        if (world.bombPlanted && !world.bombDefused) {
            glm::vec3 point = world.bombPosition;
            const float angle = randomRange(0.0f, 6.28318f);
            const float hold = (team_ == world.bombPlantingTeam) ? 1.2f : 0.35f;
            point.x += std::cos(angle) * hold;
            point.z += std::sin(angle) * hold;
            float top = 0.0f;
            if (map && map->topUnder(point.x, point.z, map->dimensions.y + 5.0f, top)) {
                point.y = top;
            }
            return point;
        }

        // No bomb yet: attackers push a site and hold it so the server can
        // plant; defenders hold a site too, waiting for the attackers.
        if (map && !map->bombSites.empty()) {
            // Attackers prefer the nearest site (short path, quicker plant);
            // defenders spread over all sites.
            size_t index = static_cast<size_t>(random01() * map->bombSites.size()) %
                           map->bombSites.size();
            if (team_ == world.attackingTeam) {
                float best = std::numeric_limits<float>::max();
                for (size_t i = 0; i < map->bombSites.size(); ++i) {
                    const float distance = glm::length(
                        glm::vec3(map->bombSites[i].position.x - position_.x, 0.0f,
                                  map->bombSites[i].position.z - position_.z));
                    if (distance < best) {
                        best = distance;
                        index = i;
                    }
                }
            }

            const glm::vec3 site = map->bombSites[index].position;
            const float hold = team_ == world.attackingTeam ? 0.0f : 2.5f;
            const float angle = randomRange(0.0f, 6.28318f);
            glm::vec3 point = site + glm::vec3(std::cos(angle) * hold, 0.0f,
                                               std::sin(angle) * hold);
            float top = 0.0f;
            if (map->topUnder(point.x, point.z, map->dimensions.y + 5.0f, top)) {
                point.y = top;
            }
            return point;
        }

        glm::vec3 site(5.0f, 0.0f, 5.0f);
        if (random01() < 0.5f) site = glm::vec3(-5.0f, 0.0f, -5.0f);
        return site;
    }

    // Deathmatch / fallback: wander somewhere reachable.
    return randomReachablePoint(map, position_);
}

void Bot::aimAt(const glm::vec3& point, float dt) {
    const glm::vec3 eye = position_ + glm::vec3(0.0f, kEyeHeight, 0.0f);
    const glm::vec3 to = point - eye;
    const float horizontal = std::sqrt(to.x * to.x + to.z * to.z);
    if (horizontal < 0.0001f && std::abs(to.y) < 0.0001f) return;

    const float desiredYaw = glm::degrees(std::atan2(to.z, to.x));
    const float desiredPitch = clampf(glm::degrees(std::atan2(to.y, horizontal)), -70.0f, 70.0f);

    const float turnRate = 140.0f + accuracy_ * 340.0f; // degrees / second
    const float maxStep = turnRate * dt;

    const float yawDelta = wrapDegrees(desiredYaw - yaw_);
    yaw_ = wrapDegrees(yaw_ + clampf(yawDelta, -maxStep, maxStep));
    pitch_ += clampf(desiredPitch - pitch_, -maxStep, maxStep);
    pitch_ = clampf(pitch_, -70.0f, 70.0f);
}

bool Bot::aimedAt(const glm::vec3& point) const {
    const glm::vec3 eye = position_ + glm::vec3(0.0f, kEyeHeight, 0.0f);
    const glm::vec3 to = point - eye;
    const float horizontal = std::sqrt(to.x * to.x + to.z * to.z);
    if (horizontal < 0.0001f && std::abs(to.y) < 0.0001f) return true;
    const float desiredYaw = glm::degrees(std::atan2(to.z, to.x));
    const float desiredPitch = glm::degrees(std::atan2(to.y, horizontal));
    return std::abs(wrapDegrees(desiredYaw - yaw_)) < 8.0f &&
           std::abs(desiredPitch - pitch_) < 7.0f;
}

void Bot::fireAt(const Target& target, float distance, std::vector<Action>& actions) {
    const glm::vec3 eye = position_ + glm::vec3(0.0f, kEyeHeight, 0.0f);
    const glm::vec3 aimPoint = target.feet + glm::vec3(0.0f, kBodyCentre, 0.0f);
    glm::vec3 base = aimPoint - eye;
    const float baseLength = glm::length(base);
    if (baseLength < 0.001f) return;
    base /= baseLength;

    const bool useShotgun = distance <= 9.0f && random01() < 0.45f;
    const float rangeFactor = clampf(1.0f - distance / kSightRange, 0.35f, 1.0f);
    const float hitChance = clampf(accuracy_ * rangeFactor, 0.0f, 0.99f);

    // Orthonormal basis around the aim direction for spread offsets.
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(base, up);
    if (glm::length(right) < 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    right = glm::normalize(right);
    const glm::vec3 up2 = glm::normalize(glm::cross(right, base));

    // Visual projectile origin: the weapon muzzle, not the eyes. The AI still
    // aims from the eye, but spawning the visible tracer/bullet at eye height
    // made bot shots look like they appeared in mid-air next to the gun.
    const float barrelLength = useShotgun ? 0.55f : 0.45f;
    const glm::vec3 muzzle = position_ + glm::vec3(0.0f, 1.32f, 0.0f) +
                             right * 0.22f + base * barrelLength;

    auto directionWithSpread = [&](bool hit) {
        const float missBoost = hit ? 0.25f : 1.0f;
        const float maxAngle = glm::radians((0.4f + (1.0f - accuracy_) * 9.0f) * missBoost);
        const float angle = randomRange(0.0f, maxAngle);
        const float roll = randomRange(0.0f, 6.28318f);
        const glm::vec3 dir = base + right * (std::tan(angle) * std::cos(roll))
                                  + up2 * (std::tan(angle) * std::sin(roll));
        return glm::normalize(dir);
    };

    if (useShotgun) {
        // Five pellets, each with its own hit roll.
        constexpr int kPellets = 5;
        for (int i = 0; i < kPellets; ++i) {
            const bool hit = random01() < hitChance;
            Action action;
            action.type = Action::FireBullet;
            action.origin = muzzle;
            action.velocity = directionWithSpread(hit) * 110.0f;
            action.lifetime = 1.0f;
            action.projectileType = 3;
            action.weaponId = 3;
            if (hit) {
                action.victimId = target.id;
                action.damage = 10;
            }
            actions.push_back(action);
        }
        fireTimer_ = randomRange(1.0f, 1.4f);
    } else {
        const bool hit = random01() < hitChance;
        Action action;
        action.type = Action::FireBullet;
        action.origin = muzzle;
        action.velocity = directionWithSpread(hit) * 150.0f;
        action.lifetime = 3.0f;
        action.projectileType = 0;
        action.weaponId = 0;
        if (hit) {
            action.victimId = target.id;
            action.damage = 25;
        }
        actions.push_back(action);
        fireTimer_ = randomRange(0.35f, 0.85f);
    }
}

void Bot::tryThrowable(const Target& target, float distance, std::vector<Action>& actions) {
    if (distance < 5.0f || distance > 28.0f) return;

    const bool throwGrenade = grenadeTimer_ <= 0.0f && grenadeAmmo_ > 0;
    const bool throwFlash = flashTimer_ <= 0.0f && flashAmmo_ > 0;
    if (!throwGrenade && !throwFlash) return;

    const glm::vec3 eye = position_ + glm::vec3(0.0f, kEyeHeight, 0.0f);
    glm::vec3 targetPoint = target.feet + glm::vec3(0.0f, 0.25f, 0.0f);
    glm::vec3 flat = targetPoint - eye;
    flat.y = 0.0f;
    const float horizontal = glm::length(flat);
    if (horizontal < 0.001f) return;
    flat /= horizontal;

    // Accuracy also affects the throw: low-accuracy bots miss by more.
    const float missAngle = glm::radians((1.0f - accuracy_) * 10.0f);
    const float angle = randomRange(-missAngle, missAngle);
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);
    const glm::vec3 dir(flat.x * cosA - flat.z * sinA, 0.0f, flat.x * sinA + flat.z * cosA);

    const float speed = 16.0f;
    const float flightTime = std::max(0.45f, horizontal / speed);
    const float vertical = (targetPoint.y - eye.y + 0.5f * 15.0f * flightTime * flightTime) / flightTime;

    Action action;
    action.origin = eye;
    action.velocity = glm::vec3(dir.x * speed, vertical, dir.z * speed);
    action.lifetime = 4.0f;

    if (throwFlash) {
        action.type = Action::ThrowFlashbang;
        action.projectileType = 2;
        action.weaponId = 2;
        flashTimer_ = randomRange(16.0f, 28.0f);
        --flashAmmo_;
    } else {
        action.type = Action::ThrowGrenade;
        action.projectileType = 1;
        action.weaponId = 1;
        grenadeTimer_ = randomRange(10.0f, 18.0f);
        --grenadeAmmo_;
    }
    actions.push_back(action);
}

bool Bot::outsideBounds(const glm::vec3& feet, const BotMap* map) const {
    if (!map) return false;
    const float halfX = map->dimensions.x * 0.5f - 0.5f;
    const float halfZ = map->dimensions.z * 0.5f - 0.5f;
    return feet.x < -halfX || feet.x > halfX || feet.z < -halfZ || feet.z > halfZ;
}

void Bot::moveHorizontal(float dt, const BotMap* map, const glm::vec3& dir, float speed) {
    glm::vec3 flat = horizontalNormalized(dir);
    if (glm::length(flat) < 0.0001f) return;

    const float step = speed * dt;
    const glm::vec3 before = position_;

    auto tryAxis = [&](int axis, float amount) {
        if (amount == 0.0f) return true;
        const float oldValue = position_[axis];
        position_[axis] += amount;
        if (!map) return true;

        if (!map->bodyBlocked(position_) && !outsideBounds(position_, map)) return true;

        // Small step-up over a kerb / stair (only while walking on the ground).
        if (grounded_) {
            const float oldY = position_.y;
            position_.y += kStepUp;
            if (!map->bodyBlocked(position_) && !outsideBounds(position_, map)) return true;
            position_.y = oldY;
        }
        position_[axis] = oldValue;
        return false;
    };

    const bool movedX = tryAxis(0, flat.x * step);
    const bool movedZ = tryAxis(2, flat.z * step);

    if (!movedX && !movedZ) {
        stuckTimer_ += dt;
    } else {
        const float moved = glm::length(glm::vec3(position_.x - before.x, 0.0f, position_.z - before.z));
        if (moved < step * 0.2f) stuckTimer_ += dt;
        else stuckTimer_ = std::max(0.0f, stuckTimer_ - dt * 2.0f);
    }

    if (stuckTimer_ > 0.8f) {
        // Jump over low obstacles and pick a new destination.
        if (grounded_) {
            velocity_.y = 4.5f;
            grounded_ = false;
        }
        stuckTimer_ = 0.0f;
        hasObjective_ = false;
        objectiveTimer_ = 0.0f;
        wanderTimer_ = 0.0f;
        clearPath();
    }
}

void Bot::updateVertical(float dt, const BotMap* map) {
    if (!map) {
        position_.y = 0.0f;
        velocity_.y = 0.0f;
        grounded_ = true;
        return;
    }
    if (map->blocks.empty()) {
        // Empty/unsupported world: treat y = 0 as a virtual floor.
        position_.y = 0.0f;
        velocity_.y = 0.0f;
        grounded_ = true;
        return;
    }

    float support = 0.0f;
    if (grounded_) {
        if (map->groundBelow(position_, kBodyHalfWidth, kGroundSnap, support)) {
            position_.y = support;
            velocity_.y = 0.0f;
        } else {
            grounded_ = false;
        }
    }

    if (!grounded_) {
        // Move first, then sweep from the start height. A fast fall can move
        // more than kGroundSnap in one tick; checking only the new position
        // would let the body pass through the floor and fall out of the map.
        const float startY = position_.y;
        velocity_.y -= kGravity * dt;
        position_.y += velocity_.y * dt;

        if (velocity_.y <= 0.0f) {
            const float drop = std::max(0.0f, startY - position_.y);
            glm::vec3 probe = position_;
            probe.y = startY;
            if (map->groundBelow(probe, kBodyHalfWidth, drop + kGroundSnap, support) &&
                position_.y <= support + 0.02f) {
                position_.y = support;
                velocity_.y = 0.0f;
                grounded_ = true;
            }
        }
        if (position_.y < -20.0f) {
            position_.y = 0.0f;
            velocity_.y = 0.0f;
            grounded_ = true;
        }
    }
}

glm::vec3 Bot::randomReachablePoint(const BotMap* map, const glm::vec3& origin) {
    if (!map) return origin;
    glm::vec3 navPoint(0.0f);
    if (map->randomNavPoint(rng_, navPoint)) return navPoint;

    const float halfX = std::max(1.0f, map->dimensions.x * 0.5f - 2.0f);
    const float halfZ = std::max(1.0f, map->dimensions.z * 0.5f - 2.0f);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const float x = randomRange(-halfX, halfX);
        const float z = randomRange(-halfZ, halfZ);
        float top = 0.0f;
        if (!map->topUnder(x, z, map->dimensions.y + 5.0f, top)) continue;
        const glm::vec3 feet(x, top, z);
        if (map->standable(feet)) return feet;
    }
    return origin;
}

void Bot::clearPath() {
    path_.clear();
    pathIndex_ = 0;
    pathGoal_ = position_;
    pathTimer_ = 0.0f;
}

glm::vec3 Bot::pathDirection(const BotMap* map, const glm::vec3& goal, float dt) {
    pathTimer_ -= dt;
    const glm::vec3 flatGoalDelta(goal.x - pathGoal_.x, 0.0f, goal.z - pathGoal_.z);
    const bool needPath = path_.empty() || pathTimer_ <= 0.0f ||
                          glm::length(flatGoalDelta) > 2.0f;

    if (needPath && map) {
        std::vector<glm::vec3> newPath;
        if (map->findPath(position_, goal, newPath)) {
            path_ = std::move(newPath);
        } else {
            path_.clear();
        }
        pathIndex_ = 0;
        pathGoal_ = goal;
        pathTimer_ = 0.6f;
    }

    // Skip waypoints we have reached (a little vertical slack covers stairs).
    while (pathIndex_ < path_.size()) {
        const glm::vec3 delta = path_[pathIndex_] - position_;
        const float horizontal = glm::length(glm::vec3(delta.x, 0.0f, delta.z));
        if (horizontal < 0.7f && std::abs(delta.y) < 1.0f) {
            ++pathIndex_;
            continue;
        }
        break;
    }

    if (pathIndex_ < path_.size()) {
        const glm::vec3 delta = path_[pathIndex_] - position_;
        const glm::vec3 flat = horizontalNormalized(delta);
        if (glm::length(flat) > 0.001f) return flat;
    }
    return horizontalNormalized(goal - position_);
}

void Bot::update(float dt, const World& world, std::vector<Action>& actions) {
    if (!alive_) {
        respawnTimer_ -= dt;
        return;
    }
    dt = clampf(dt, 0.001f, 0.1f);
    gameMode_ = world.gameMode;

    if (fireTimer_ > 0.0f) fireTimer_ -= dt;
    if (grenadeTimer_ > 0.0f) grenadeTimer_ -= dt;
    if (flashTimer_ > 0.0f) flashTimer_ -= dt;
    if (reactionTimer_ > 0.0f) reactionTimer_ -= dt;
    if (lastSeenTimer_ > 0.0f) lastSeenTimer_ -= dt;

    // ── Blinded: no aiming, random wandering ────────────────────────────────
    if (blindTimer_ > 0.0f) {
        blindTimer_ -= dt;
        lastTargetId_ = 0;

        wanderTimer_ -= dt;
        if (wanderTimer_ <= 0.0f) {
            wanderTarget_ = randomReachablePoint(world.map, position_);
            wanderTimer_ = randomRange(0.5f, 1.2f);
            yaw_ = randomRange(-180.0f, 180.0f);
            clearPath();
        }

        glm::vec3 dir = pathDirection(world.map, wanderTarget_, dt);
        if (glm::length(dir) < 0.001f) {
            const float angle = randomRange(0.0f, 6.28318f);
            dir = glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
        }
        // Randomly stagger around while blind.
        const float turn = randomRange(-240.0f, 240.0f) * dt;
        yaw_ = wrapDegrees(yaw_ + turn);
        pitch_ = 0.0f;
        moveHorizontal(dt, world.map, dir, 1.8f);
        updateVertical(dt, world.map);
        return;
    }

    const Target* target = selectTarget(world);
    glm::vec3 moveDir(0.0f);
    float moveSpeed = 3.8f;

    if (target) {
        clearPath();
        if (target->id != lastTargetId_) {
            lastTargetId_ = target->id;
            // Slower reaction for low accuracy (also makes them feel human).
            reactionTimer_ = 0.20f + (1.0f - accuracy_) * 1.10f;
        }
        lastSeenPos_ = target->feet;
        lastSeenTimer_ = 5.0f;

        const glm::vec3 aimPoint = target->feet + glm::vec3(0.0f, kBodyCentre, 0.0f);
        aimAt(aimPoint, dt);
        const float distance = glm::length(target->feet - position_);

        strafeTimer_ -= dt;
        if (strafeTimer_ <= 0.0f) {
            strafeTimer_ = randomRange(0.45f, 1.4f);
            strafeDir_ = (random01() < 0.5f) ? -1 : 1;
        }

        const glm::vec3 toTarget = horizontalNormalized(target->feet - position_);
        const glm::vec3 strafe(-toTarget.z * static_cast<float>(strafeDir_), 0.0f,
                                toTarget.x * static_cast<float>(strafeDir_));

        if (distance > 18.0f) {
            moveDir = toTarget;
        } else if (distance < 5.0f) {
            moveDir = -toTarget * 0.7f + strafe * 0.5f;
        } else {
            moveDir = strafe + toTarget * 0.15f;
        }
        moveSpeed = 3.6f;

        if (reactionTimer_ <= 0.0f && fireTimer_ <= 0.0f && aimedAt(aimPoint)) {
            fireAt(*target, distance, actions);
        }
        tryThrowable(*target, distance, actions);
    } else {
        lastTargetId_ = 0;
        reactionTimer_ = std::max(reactionTimer_, 0.15f);

        if (lastSeenTimer_ > 0.0f) {
            moveDir = pathDirection(world.map, lastSeenPos_, dt);
            aimAt(lastSeenPos_ + glm::vec3(0.0f, kBodyCentre, 0.0f), dt);
        } else {
            objectiveTimer_ -= dt;

            auto objectiveDistance = [&]() {
                if (!hasObjective_) return std::numeric_limits<float>::max();
                return glm::length(glm::vec3(objective_.x - position_.x, 0.0f,
                                             objective_.z - position_.z));
            };

            float distance = objectiveDistance();
            // In bomb defusal an attacker standing on the site (or anyone
            // standing at a planted bomb) must stop there long enough for the
            // server to finish planting / defusing, instead of pathing around.
            const bool bombObjective = world.gameMode == 2 &&
                ((world.bombPlanted && !world.bombDefused) ||
                 team_ == world.attackingTeam);
            const bool holding = bombObjective && hasObjective_ && distance < 1.6f;

            if (!hasObjective_ || (!holding && (objectiveTimer_ <= 0.0f || distance < 1.2f))) {
                objective_ = chooseObjective(world);
                hasObjective_ = true;
                objectiveTimer_ = randomRange(5.0f, 10.0f);
                distance = objectiveDistance();
                clearPath();
            }

            const bool atObjective = world.gameMode == 2 && hasObjective_ && distance < 1.6f;
            if (atObjective) {
                moveDir = glm::vec3(0.0f);
                aimAt(objective_ + glm::vec3(0.0f, kBodyCentre, 0.0f), dt);
            } else {
                moveDir = pathDirection(world.map, objective_, dt);
                if (glm::length(moveDir) > 0.001f) {
                    aimAt(position_ + moveDir * 4.0f + glm::vec3(0.0f, kBodyCentre, 0.0f), dt);
                }
            }
        }
    }

    moveHorizontal(dt, world.map, moveDir, moveSpeed);
    updateVertical(dt, world.map);
}

float distanceToBody(const glm::vec3& point, const glm::vec3& feet, float height) {
    const glm::vec3 mn(feet.x - kBodyHalfWidth, feet.y, feet.z - kBodyHalfWidth);
    const glm::vec3 mx(feet.x + kBodyHalfWidth, feet.y + height, feet.z + kBodyHalfWidth);
    const glm::vec3 closest(clampf(point.x, mn.x, mx.x),
                            clampf(point.y, mn.y, mx.y),
                            clampf(point.z, mn.z, mx.z));
    return glm::length(point - closest);
}

} // namespace bots
