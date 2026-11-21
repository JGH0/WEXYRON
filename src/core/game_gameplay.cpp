#include "game.hpp"
#include "../game/local_bots.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spatialkit/geometry.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>
#include <GLFW/glfw3.h>
#include <array>

namespace {

// Gameplay works in GLM; SpatialKit deliberately has its own tiny math types
// so it stays engine-agnostic. These two helpers are the only bridge.
inline spatialkit::Vec3 toSpatial(const glm::vec3& value) {
    return spatialkit::Vec3(value.x, value.y, value.z);
}

inline glm::vec3 toGlm(const spatialkit::Vec3& value) {
    return glm::vec3(value.x, value.y, value.z);
}

} // namespace

// ---------------------------------------------------------------------------
// Player body: one box shared by rendering and hit detection. Positions are
// feet positions, so a body standing at `feet` spans from the ground up to
// bodyHeight and the camera sits eyeHeight above it.
// ---------------------------------------------------------------------------

void Game::bodyBox(const glm::vec3& feet, glm::vec3& boxMin, glm::vec3& boxMax,
                   float height) {
    const float half = bodyWidth * 0.5f;
    boxMin = glm::vec3(feet.x - half, feet.y, feet.z - half);
    boxMax = glm::vec3(feet.x + half, feet.y + height, feet.z + half);
}

float Game::distanceToBody(const glm::vec3& point, const glm::vec3& feet, float height) {
    glm::vec3 boxMin, boxMax;
    bodyBox(feet, boxMin, boxMax, height);
    // Distance from the point to an axis-aligned box (0 when inside it).
    const glm::vec3 d = glm::max(glm::max(boxMin - point, point - boxMax), glm::vec3(0.0f));
    return glm::length(d);
}

bool Game::pointHitsBody(const glm::vec3& point, const glm::vec3& feet, float slack,
                         float height) {
    return distanceToBody(point, feet, height) <= slack;
}

Game::ProjectileSweepHit Game::sweepProjectileAgainstBody(const glm::vec3& from,
                                                          const glm::vec3& to,
                                                          const glm::vec3& feet,
                                                          float radius,
                                                          float height) {
    ProjectileSweepHit result;

    glm::vec3 boxMin, boxMax;
    bodyBox(feet, boxMin, boxMax, height);

    // Fatten the body by the projectile radius. This is where the old
    // `bodyHitSlack` lives now, and it makes the swept test tolerant of
    // near-misses without ever skipping the segment in between frames.
    const glm::vec3 margin(radius, radius, radius);
    const spatialkit::AABB body{toSpatial(boxMin - margin),
                                toSpatial(boxMax + margin)};

    const spatialkit::RayHit hit =
        spatialkit::intersectSegmentAABB(toSpatial(from), toSpatial(to), body);
    if (!hit.hit) {
        return result;
    }

    result.hit = true;
    result.t = hit.t;
    result.point = toGlm(hit.point);
    return result;
}

glm::vec3 Game::projectileSegmentStart(const Projectile& projectile) const {
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (&projectiles[i] != &projectile) continue;
        // createProjectile() seeds this slot with the spawn position and every
        // map check refreshes it, so it is always the start of the current
        // frame's segment, including the very first frame after spawning.
        return projectilePrevPos[i];
    }
    return projectile.position;
}

// ---------------------------------------------------------------------------
// Player body collision
//
// The player is a rigid box from the toes to the head (feet position + body
// size), not a point with a radius. Map geometry collides with the whole box:
// a floating slab at knee height stops the body whether it hits the toes, the
// legs, the torso or the head. Box-vs-block tests run on the map's block grid
// (Map::boxOverlapsBlock), so this costs the same as the old point test.
// ---------------------------------------------------------------------------

float Game::bodyHeightForStance(uint8_t stance) {
    switch (stance) {
        case 1:  return crouchBodyHeight;
        case 2:  return proneBodyHeight;
        default: return bodyHeight;
    }
}

float Game::eyeHeightForStance(uint8_t stance) {
    switch (stance) {
        case 1:  return crouchEyeHeight;
        case 2:  return proneEyeHeight;
        default: return eyeHeight;
    }
}

float Game::currentEyeHeight() const {
    return eyeHeightForStance(static_cast<uint8_t>(stance_));
}

float Game::currentBodyHeight() const {
    return bodyHeightForStance(static_cast<uint8_t>(stance_));
}

float Game::currentBodyCenterHeight() const {
    return currentBodyHeight() * 0.5f;
}

void Game::stanceBodyLocalBox(uint8_t stance, glm::vec3& boxMin, glm::vec3& boxMax) {
    const float half = bodyWidth * 0.5f;
    boxMin = glm::vec3(-half, 0.0f, -half);
    if (stance == 2) {
        // A prone body is rotated forward around the feet by the renderer, so
        // its hit box extends along local +Z instead of upwards.
        boxMax = glm::vec3(half, proneBodyHeight, bodyHeight);
    } else {
        boxMax = glm::vec3(half, bodyHeightForStance(stance), half);
    }
}

glm::vec3 Game::worldToBodyLocal(const glm::vec3& point, const glm::vec3& feet,
                                 float yawDeg) {
    const float yaw = glm::radians(yawDeg);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const glm::vec3 d = point - feet;
    return glm::vec3(d.x * (-sy) + d.z * cy, d.y,
                     d.x * cy + d.z * sy);
}

glm::vec3 Game::bodyLocalToWorld(const glm::vec3& local, const glm::vec3& feet,
                                 float yawDeg) {
    const float yaw = glm::radians(yawDeg);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    return feet + glm::vec3(local.x * (-sy) + local.z * cy, local.y,
                            local.x * cy + local.z * sy);
}

float Game::distanceToStanceBody(const glm::vec3& point, const glm::vec3& feet,
                                 float yawDeg, uint8_t stance) {
    glm::vec3 boxMin, boxMax;
    stanceBodyLocalBox(stance, boxMin, boxMax);
    const glm::vec3 local = worldToBodyLocal(point, feet, yawDeg);
    const glm::vec3 d = glm::max(glm::max(boxMin - local, local - boxMax),
                                 glm::vec3(0.0f));
    return glm::length(d);
}

Game::ProjectileSweepHit Game::sweepProjectileAgainstStanceBody(
    const glm::vec3& from, const glm::vec3& to, const glm::vec3& feet,
    float yawDeg, float radius, uint8_t stance) {
    ProjectileSweepHit result;

    glm::vec3 boxMin, boxMax;
    stanceBodyLocalBox(stance, boxMin, boxMax);
    const glm::vec3 margin(radius, radius, radius);
    const spatialkit::AABB body{toSpatial(boxMin - margin),
                                toSpatial(boxMax + margin)};

    const glm::vec3 localFrom = worldToBodyLocal(from, feet, yawDeg);
    const glm::vec3 localTo = worldToBodyLocal(to, feet, yawDeg);
    const spatialkit::RayHit hit =
        spatialkit::intersectSegmentAABB(toSpatial(localFrom), toSpatial(localTo), body);
    if (!hit.hit) return result;

    result.hit = true;
    result.t = hit.t;
    result.point = bodyLocalToWorld(toGlm(hit.point), feet, yawDeg);
    return result;
}

bool Game::bodyFitsHeight(const glm::vec3& feet, float height) const {
    if (!currentMap) return true;
    const float half = bodyWidth * 0.5f;
    const glm::vec3 mn(feet.x - half, feet.y, feet.z - half);
    const glm::vec3 mx(feet.x + half, feet.y + height, feet.z + half);
    return !currentMap->boxOverlapsBlock(mn, mx);
}

bool Game::canStandAtPlayer() const {
    return bodyFitsHeight(playerPos, bodyHeight);
}

void Game::bodyBounds(const glm::vec3& feet, glm::vec3& mn, glm::vec3& mx) const {
    const float half = bodyWidth * 0.5f;
    mn = glm::vec3(feet.x - half, feet.y, feet.z - half);
    mx = glm::vec3(feet.x + half, feet.y + currentBodyHeight(), feet.z + half);
}

bool Game::bodyBlockedAt(const glm::vec3& feet) const {
    if (!currentMap) return false;
    glm::vec3 mn, mx;
    bodyBounds(feet, mn, mx);
    return currentMap->boxOverlapsBlock(mn, mx);
}

bool Game::isWalkableSlope(const glm::vec3& normal) {
    return normal.y >= kMaxWalkableSlopeCos;
}

bool Game::groundSurfaceUnderBox(const glm::vec3& min, const glm::vec3& max,
                                 float maxDrop, float maxRise,
                                 float& height, glm::vec3& normal) const {
    if (!currentMap) return false;

    const float rise = std::max(0.0f, maxRise);
    const float drop = std::max(0.0f, maxDrop) + rise;
    if (drop <= 0.0f) return false;

    const float fromY = min.y + rise;
    const glm::vec2 samples[5] = {
        {(min.x + max.x) * 0.5f, (min.z + max.z) * 0.5f},
        {min.x, min.z}, {min.x, max.z}, {max.x, min.z}, {max.x, max.z}
    };

    bool found = false;
    float bestHeight = 0.0f;
    glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);

    for (const glm::vec2& p : samples) {
        const Map::GroundHit hit =
            currentMap->groundSurfaceAt(p.x, p.y, fromY, drop);
        if (!hit.hit) continue;
        if (!found || hit.height > bestHeight) {
            found = true;
            bestHeight = hit.height;
            bestNormal = hit.normal;
        }
    }

    if (!found) return false;
    height = bestHeight;
    normal = bestNormal;
    return true;
}

void Game::movePlayerBody(float dt, const glm::vec3& horizontalSpeed) {
    if (!currentMap) {
        playerPos += playerVelocity * dt;
        return;
    }

    float dx = horizontalSpeed.x * dt;
    float dz = horizontalSpeed.z * dt;
    const float dy = playerVelocity.y * dt;

    // Sub-stepping: no single horizontal step may be longer than a fraction of
    // the thinnest map block, so a fast body cannot pass through a slab
    // between two frames (the box test would then land it inside the slab).
    const float maxStep = 0.3f;
    const int steps = glm::clamp(static_cast<int>(std::ceil(
        std::max(std::abs(dx), std::abs(dz)) / maxStep)), 1, 8);
    dx /= static_cast<float>(steps);
    dz /= static_cast<float>(steps);

    glm::vec3 feet = playerPos;
    glm::vec3 mn, mx;
    bodyBounds(feet, mn, mx);

    // Vertical first (falls land on a surface), then horizontal sliding on the
    // grounded body, then any remaining vertical motion (rising past a ledge).
    // Doing the dominant motion first is what keeps a falling body from
    // catching on the lip of a surface it should land on.
    const bool falling = dy <= 0.0f;
    if (falling) {
        verticalSweep(feet, mn, mx, dy, true);
        for (int i = 0; i < steps; ++i) horizontalSweep(feet, mn, mx, dx, dz);
    } else {
        for (int i = 0; i < steps; ++i) horizontalSweep(feet, mn, mx, dx, dz);
        verticalSweep(feet, mn, mx, dy, false);
    }

    // The box never ends up inside geometry: a final push-out covers corners
    // and the few cases the per-axis moves cannot resolve.
    currentMap->resolveBox(glm::vec3(dx, dy, dz), mn, mx);
    feet = glm::vec3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);

    // The body never leaves the playable volume.
    const glm::vec3 half = currentMap->getDimensions() * 0.5f;
    const float bound = 1.0f;
    feet.x = glm::clamp(feet.x, -half.x + bound, half.x - bound);
    feet.z = glm::clamp(feet.z, -half.z + bound, half.z - bound);
    feet.y = std::max(feet.y, -30.0f);

    playerPos = feet;
}

void Game::horizontalSweep(glm::vec3& feet, glm::vec3& mn, glm::vec3& mx, float dx, float dz) {
    // One axis at a time: X then Z. A blocked axis either steps up onto a low
    // ledge (stairs/kerbs) or stops that component (so sliding along a wall
    // keeps the other). The step-up is what makes the authored staircases
    // walkable without jumping on every stair.
    auto tryMove = [&](int axis, float delta) -> bool {
        if (delta == 0.0f) return true;

        glm::vec3 tmn = mn, tmx = mx;
        if (axis == 0) { tmn.x += delta; tmx.x += delta; }
        else           { tmn.z += delta; tmx.z += delta; }

        glm::vec3 push;
        if (!currentMap->boxOverlapsBlock(tmn, tmx, push)) {
            mn = tmn; mx = tmx;
        } else {
            // Try the same horizontal move lifted onto the real surface there.
            // This covers both stairs and pitched ramps: the surface height is
            // measured from the actual (oriented) top face, not the AABB.
            // Do not require the current footprint to be clear: the front of
            // the body can already touch the low end of a ramp before the step
            // has been taken.
            const bool canStep = !isJumping && playerVelocity.y <= 0.0f;
            if (!canStep) return false;

            float support = 0.0f;
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            if (!groundSurfaceUnderBox(tmn, tmx, 0.02f, kStepHeight,
                                       support, normal)) {
                return false;
            }
            if (!isWalkableSlope(normal)) return false;
            if (support > mn.y + kStepHeight + 0.02f) return false; // too high

            // A ramp can start exactly at the feet height. Lifting by a small
            // clearance lets the body step onto its low lip even though the
            // sampled surface there is not yet above the current feet.
            const float lift = std::max(0.0f, support - mn.y) + 0.01f;
            glm::vec3 smn = tmn, smx = tmx;
            smn.y += lift;
            smx.y += lift;

            if (currentMap->boxOverlapsBlock(smn, smx, push)) return false;

            mn = smn; mx = smx;
        }

        feet = glm::vec3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
        return true;
    };

    if (!tryMove(0, dx)) playerVelocity.x = 0.0f;
    if (!tryMove(2, dz)) playerVelocity.z = 0.0f;
}

void Game::verticalSweep(glm::vec3& feet, glm::vec3& mn, glm::vec3& mx, float dy, bool falling) {
    // A vertical move is swept between the start and the target: the body
    // stops on the first surface it reaches in that direction, so a fall can
    // never pass through a slab (and lands without dropping a frame's worth).
    if (falling) {
        float support = mn.y;
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (groundSurfaceUnderBox(mn, mx, -dy + 0.02f, 0.0f, support, normal)) {
            // The body reaches the surface this step: rest exactly on it. This
            // uses the real sloped surface for pitched blocks, so landing on a
            // ramp does not stop at the AABB corner high above it.
            const float lift = support - mn.y + 0.002f;
            mn.y += lift; mx.y += lift;
            playerVelocity.y = 0.0f;
            isJumping = false;
            feet.y = mn.y;
            return;
        }
        if (dy != 0.0f) {
            glm::vec3 tmn = mn, tmx = mx;
            tmn.y += dy; tmx.y += dy;
            glm::vec3 push;
            if (!currentMap->boxOverlapsBlock(tmn, tmx, push)) {
                mn = tmn; mx = tmx;
            } else {
                playerVelocity.y = 0.0f;      // entered geometry: stop here
            }
        }
    } else if (dy > 0.0f) {
        // ceilingHeight() returns the top of the searched range when nothing
        // is above within reach (mx.y + maxRise); anything below that is real.
        const float ceiling = currentMap->ceilingHeight(mn, mx, dy + 0.05f);
        if (ceiling < mx.y + 0.01f) {
            // Already touching a surface above the head: stop.
            playerVelocity.y = 0.0f;
        } else if (ceiling - mx.y <= dy + 0.02f) {
            // The head reaches the surface this step: stop right under it.
            const float lift = ceiling - mx.y;
            if (lift > 0.0f) { mn.y += lift; mx.y += lift; }
            playerVelocity.y = 0.0f;
        } else {
            glm::vec3 tmn = mn, tmx = mx;
            tmn.y += dy; tmx.y += dy;
            glm::vec3 push;
            if (!currentMap->boxOverlapsBlock(tmn, tmx, push)) {
                mn = tmn; mx = tmx;
            } else {
                playerVelocity.y = 0.0f;
            }
        }
    }
    feet.y = mn.y;
}

// ---------------------------------------------------------------------------
// Safe placement: spawn points and "get me out of geometry" both end here, so
// a body never starts inside a block. The spot is searched near the requested
// point (up onto the surface first, then around it), and always rests on a
// floor rather than floating.
// ---------------------------------------------------------------------------

bool Game::placeAtSafePosition(glm::vec3& feet, float searchRadius) const {
    if (!currentMap) return false;

    const float half = bodyWidth * 0.5f;   // body collision half-extent

    // A body is usable when it is not inside geometry and not buried under a
    // surface it overlaps (i.e. its feet are below the top of a block it
    // stands in - the slab above such a surface would catch it next frame).
    auto usable = [&](const glm::vec3& p) {
        float floorTop;
        if (currentMap->getFloorBelow(p, half, 1.5f, floorTop) && floorTop > p.y + 0.02f) {
            return false;
        }
        return !bodyBlockedAt(p);
    };

    // Ordered candidates around the requested point:
    //   1. the requested spot itself (resting on the floor below it),
    //   2. up a few steps (out of a raised block or a pit),
    //   3. rings around it (overlapping a block side or a corner),
    //   4. the same rings lifted by one step (on top of a cover block).
    std::vector<glm::vec3> candidates;
    candidates.push_back(feet);
    for (float lift : {0.6f, 1.2f, 2.0f}) {
        candidates.push_back(feet + glm::vec3(0.0f, lift, 0.0f));
    }
    for (int ring = 1; ring <= 3; ++ring) {
        const float r = searchRadius * static_cast<float>(ring) / 3.0f;
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * glm::pi<float>() * 0.25f;
            const glm::vec3 off(std::cos(a) * r, 0.0f, std::sin(a) * r);
            candidates.push_back(feet + off);
            candidates.push_back(feet + off + glm::vec3(0.0f, 1.2f, 0.0f));
        }
    }

    for (glm::vec3 p : candidates) {
        // Resting on the floor below the candidate.
        glm::vec3 rested = p;
        if (currentMap->getFloorBelow(p, half, 100.0f, rested.y) && usable(rested)) {
            feet = rested;
            return true;
        }
        // Not restable: a body already standing in a room keeps its height.
        if (usable(p)) {
            feet = p;
            return true;
        }
    }
    return false;
}

bool Game::getRemotePlayerFeet(uint32_t id, glm::vec3& feet) const {
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        if (remotePlayers[i].active && remotePlayers[i].id == id) {
            feet = remotePlayers[i].position;   // network packets carry feet
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Damage without a projectile (blast, contact). Same rules everywhere: no
// damage while already dead, death is resolved once, and the server is told
// so it can respawn the victim.
// ---------------------------------------------------------------------------

void Game::applyPlayerDamage(float damage) {
    if (isDead || damage <= 0.0f) return;

    playerHealth -= damage;
    damageEffectTime = std::max(damageEffectTime, 1.0f);
    playSoundEffect("player_damage", playerPos + glm::vec3(0.0f, currentEyeHeight(), 0.0f), 0.9f);
    reportLocalDamage(damage);

    std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;

    if (playerHealth <= 0.0f) {
        playerHealth = 0.0f;
        dieLocally();
    }
}

void Game::reportLocalDamage(float damage, uint8_t weaponId) {
    // Damage resolved on this client (own grenade, falling, contact) is sent
    // against ourselves. The server keeps the authoritative health/death; the
    // victim's client would otherwise die locally while the server and the
    // other clients still saw a living player.
    if (damage <= 0.0f) return;
    if (!networkClient || !networkClient->isConnected()) return;
    const uint32_t self = networkClient->getClientId();
    if (self == 0) return;
    const float capped = std::min(damage, 65535.0f);
    networkClient->sendPlayerDamage(self, static_cast<uint16_t>(capped), weaponId);
}

void Game::dieLocally() {
    if (isDead) return;
    isDead = true;
    deaths_++;
    // Team kill counters only exist in Team Deathmatch. Bomb Defusal's team
    // scores are ROUND wins and must never be overwritten with kill counts.
    if (currentGameMode == TEAM_VS_TEAM) {
        if (localBots_) {
            localBots_->recordPlayerDeath(playerTeam & 1);
            teamAScore_ = localBots_->teamAKills();
            teamBScore_ = localBots_->teamBKills();
        } else if (playerTeam == 0) {
            ++teamBScore_;
        } else {
            ++teamAScore_;
        }
    }
    checkMatchEnd();
    playerVelocity = glm::vec3(0.0f);
    // A dying player cannot finish planting/defusing.
    isPlanting = false;
    isDefusing = false;
    plantProgress = 0.0f;
    defuseProgress = 0.0f;
    stance_ = STANCE_STANDING;
    eyeHeightCurrent_ = eyeHeight;
    playSoundEffect("player_death", playerPos + glm::vec3(0.0f, currentEyeHeight(), 0.0f));

    if (bombDefusalMode) {
        spectatorMode = true;
        std::cout << "Player died! Entering spectator mode." << std::endl;
    } else {
        gameState = DEAD;
    }

    // The server keeps the authoritative health: it sees health 0 and puts us
    // back at a spawn point (and broadcasts the respawned state to everyone).
    if (networkClient && networkClient->isConnected()) {
        const glm::vec3 eye = playerPos + glm::vec3(0.0f, currentEyeHeight(), 0.0f);
        networkClient->sendPlayerInput(0.0f, 0.0f, camera.Yaw, camera.Pitch, false, false,
                                       static_cast<uint8_t>(currentWeapon),
                                       eye.x, eye.y, eye.z, false,
                                       static_cast<uint8_t>(currentGameMode),
                                       static_cast<uint8_t>(matchFormat_),
                                       static_cast<uint8_t>(stance_));
    }
    std::cout << "Player defeated!" << std::endl;
}

void Game::respawnLocally() {
    resetGame();
    // resetGame() also rebuilds the practice bots; they have no place in a
    // networked match (other players are the opponents there).
    if (networkClient && networkClient->isConnected()) {
        clearBots();
        // Our own state packets are ignored (we control ourselves), but the
        // inputs that follow carry the new spawn position to the server.
        std::cout << "Respawned" << std::endl;
    }
}

void Game::clearBots() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive  = false;
    }
    activeEnemies = 0;
}

void Game::updatePlayerPhysics(float deltaTime) {
    // AUTOMATIC UNSTUCK: if the body is inside a solid block (spawned in
    // geometry, or pushed into it), move it out to a safe spot. Only a body
    // that is really overlapping geometry triggers this - standing on a
    // floor does not (see bodyBlockedAt).
    if (!isDead && bodyBlockedAt(playerPos)) {
        const glm::vec3 before = playerPos;
        if (placeAtSafePosition(playerPos)) {
            std::cout << "UNSTUCK: moved from (" << before.x << ", " << before.y << ", "
                      << before.z << ") to (" << playerPos.x << ", " << playerPos.y << ", "
                      << playerPos.z << ")" << std::endl;
        }
        playerVelocity = glm::vec3(0.0f, 0.0f, 0.0f);
        playerDamageCooldown = std::max(playerDamageCooldown, 0.25f);
    }
    
    // Check for death by falling. A dead player is not falling again: the
    // respawn code (or the death screen) owns the body until it comes back.
    if (playerPos.y < -20.0f && !isDead) {
        playerHealth = 0.0f;
        std::cout << "Player fell out of the world and died!" << std::endl;
        reportLocalDamage(100.0f);
        dieLocally();
        return;
    }
    
    // ── Ground state ────────────────────────────────────────────────────────
    // isOnGround() is decided from where the body is *before* the gravity step,
    // so the jump pressed this frame can still act (the input runs before the
    // physics). The state is refreshed below from where the body ends up.
    const float feetBefore = playerPos.y;
    const bool groundedBefore = isOnGround();

    // Apply gravity
    playerVelocity.y -= 9.8f * deltaTime;

    // Horizontal speed comes from the input (processGameInput set it); the
    // vertical speed comes from gravity and jumps. Downhill momentum from a
    // steep slope is added on top of the input.
    const glm::vec3 horizontal(playerVelocity.x + slopeSlideVelocity_.x, 0.0f,
                               playerVelocity.z + slopeSlideVelocity_.z);
    movePlayerBody(deltaTime, horizontal);

    // ── Slopes / ramps ──────────────────────────────────────────────────────
    // Follow the real surface under the body. Walkable ramps (up to 40 deg)
    // snap the feet up/down so they can be climbed; steeper faces are not
    // support, and accelerate the body downhill.
    {
        glm::vec3 gmn, gmx;
        bodyBounds(playerPos, gmn, gmx);

        float support = 0.0f;
        glm::vec3 groundNormal(0.0f, 1.0f, 0.0f);
        bool steepContact = false;
        // Large step-follow is for walking. A fast fall must not snap down by
        // half a metre just because the floor came within the step range; the
        // vertical sweep already lands it continuously.
        const bool walking = groundedBefore && playerVelocity.y > -1.5f;
        const float rise = walking ? kStepHeight : 0.02f;
        const float drop = walking ? kStepHeight : 0.02f;

        if (!isJumping && playerVelocity.y <= 0.0f &&
            groundSurfaceUnderBox(gmn, gmx, drop, rise, support, groundNormal)) {
            const float gap = playerPos.y - support;
            if (gap <= drop + 0.02f && gap >= -rise - 0.02f) {
                if (isWalkableSlope(groundNormal)) {
                    playerPos.y = support + 0.002f;
                    playerVelocity.y = 0.0f;
                    isJumping = false;
                } else {
                    // Too steep to stand on: stay on the surface and slide.
                    steepContact = true;
                    playerPos.y = support + 0.002f;
                    playerVelocity.y = 0.0f;
                    isJumping = false;

                    glm::vec3 downhill(groundNormal.x, 0.0f, groundNormal.z);
                    const float len = glm::length(downhill);
                    if (len > 1.0e-4f) {
                        downhill /= len;
                        const float sinSlope = std::sqrt(std::max(
                            0.0f, 1.0f - groundNormal.y * groundNormal.y));
                        slopeSlideVelocity_ += downhill * (9.8f * sinSlope * deltaTime);
                        const float speed = glm::length(slopeSlideVelocity_);
                        const float maxSlideSpeed = 7.0f;
                        if (speed > maxSlideSpeed) {
                            slopeSlideVelocity_ *= maxSlideSpeed / speed;
                        }
                    }
                }
            }
        }

        // Flat ground / air: drop the downhill momentum quickly instead of
        // letting the player coast forever after a ramp.
        if (!steepContact) {
            slopeSlideVelocity_ *= std::exp(-6.0f * deltaTime);
            if (glm::length(slopeSlideVelocity_) < 0.03f) {
                slopeSlideVelocity_ = glm::vec3(0.0f);
            }
        }
    }

    // Standing after this frame: the feet rest on a surface (at most a small
    // step below them) and the body is not falling.
    const bool onGround = isOnGround();

    // Debug output
    static int frameCount = 0;
    if (frameCount++ % 120 == 0) {
        std::cout << "Physics - Pos: (" << playerPos.x << ", " << playerPos.y << ", " << playerPos.z 
                  << ") Vel: (" << playerVelocity.x << ", " << playerVelocity.y << ", " << playerVelocity.z 
                  << ") - " << (onGround ? "ON GROUND" : "IN AIR/FALLING") << std::endl;
    }
    
    // Update the eye height towards the stance (crouch / prone). The camera
    // follows the body exactly in X/Z; only the vertical eye offset eases, so
    // going prone does not snap the view to the floor in one frame.
    const float targetEye = currentEyeHeight();
    const float eyeBlend = 1.0f - std::exp(-deltaTime * 16.0f);
    eyeHeightCurrent_ += (targetEye - eyeHeightCurrent_) * eyeBlend;
    if (std::abs(eyeHeightCurrent_ - targetEye) < 0.005f) {
        eyeHeightCurrent_ = targetEye;
    }

    // Walking up stairs/kerbs snaps the body up by a whole step. Keep the
    // camera a little behind that vertical snap (visual only) so continuous
    // stairs do not jolt the view, then ease it back out.
    const float feetDelta = playerPos.y - feetBefore;
    if (groundedBefore && !isJumping &&
        std::abs(feetDelta) > 0.08f &&
        std::abs(feetDelta) <= kStepHeight + 0.05f) {
        stepSmoothOffset_ -= feetDelta;
        stepSmoothOffset_ = glm::clamp(stepSmoothOffset_, -kStepHeight, kStepHeight);
    }
    const float stepBlend = 1.0f - std::exp(-deltaTime * 22.0f);
    stepSmoothOffset_ += (0.0f - stepSmoothOffset_) * stepBlend;
    if (std::abs(stepSmoothOffset_) < 0.002f) stepSmoothOffset_ = 0.0f;

    // The camera follows the body exactly. (A smoothed X/Z lag made the map
    // look doubled: the world is drawn once mid-frame with this position and
    // once at the end, so any lag between the two shifted the two copies.)
    camera.Position = glm::vec3(playerPos.x,
                                playerPos.y + eyeHeightCurrent_ + stepSmoothOffset_,
                                playerPos.z);
    camera.updateCameraVectors();
}

// ---------------------------------------------------------------------------
// Ground and landing
// ---------------------------------------------------------------------------

bool Game::isSupportedAt(const glm::vec3& feet) const {
    if (!currentMap) return false;
    // The body rests on the highest real surface under its footprint, up to
    // one small step below the feet (a few millimetres above them are float
    // error after a snap).
    glm::vec3 mn, mx;
    bodyBounds(feet, mn, mx);
    float support = feet.y;
    glm::vec3 normal(0.0f, 1.0f, 0.0f);
    if (!groundSurfaceUnderBox(mn, mx, kGroundSnap + 0.02f, 0.02f,
                               support, normal)) {
        return false;
    }
    return isWalkableSlope(normal) &&
           support <= feet.y + 0.02f && feet.y - support <= kGroundSnap + 0.02f;
}

bool Game::isOnGround() const {
    return !isDead && currentMap != nullptr && playerVelocity.y <= 0.0f &&
           !bodyBlockedAt(playerPos) && isSupportedAt(playerPos);
}

void Game::handleJump() {
    // A jump only starts from the ground: standing on a surface, and not still
    // rising from the previous jump. isJumping guards the air; the ground check
    // is what lets a held jump key jump again on every landing.
    if (!isOnGround()) return;
    if (isJumping) return;

    playerVelocity.y = kJumpSpeed;
    isJumping = true;
    playSoundEffect("jump", playerPos, 0.8f);
    std::cout << "Jump triggered! Player y: " << playerPos.y << std::endl;
}

bool Game::createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime, uint32_t owner, int type) {
    // Find an inactive projectile slot
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = position;
            projectiles[i].velocity = velocity;
            projectiles[i].lifetime = lifetime;
            projectiles[i].active = true;
            projectiles[i].detonated = false;
            projectiles[i].owner = owner;
            projectiles[i].type = type;
            activeProjectiles++;
            // Reset swept-collision tracking state so stale prev positions
            // from a previous projectile in this slot don't cause false collisions.
            projectilePrevPosInitialized[i] = false;
            projectilePrevPos[i] = position;

            // Our own shot: the other clients see exactly this projectile.
            // Relaying the real spawn (instead of letting the server rebuild a
            // shot from the input flags) keeps the type, pellet count and ammo
            // use identical on every client.
            if (owner == 0 && networkClient && networkClient->isConnected()) {
                networkClient->sendProjectileSpawn(position, velocity, lifetime,
                                                  static_cast<uint8_t>(type));
            }
            return true;
        }
    }
    return false;
}

void Game::shoot() {
    if (currentWeaponInstance && currentWeaponInstance->canShoot()) {
        currentWeaponInstance->shoot(this);
        // Weapon viewmodel kick: pistols snap a little, the pump shotgun kicks
        // hard, throwables barely move the hand.
        float kick = 0.45f;
        if (currentWeapon == PUMP_SHOTGUN)      kick = 0.95f;
        else if (currentWeapon == SNIPER)       kick = 1.0f;
        else if (currentWeapon == GRENADE ||
                 currentWeapon == FLASHBANG)    kick = 0.25f;
        weaponRecoil_ = std::min(1.0f, weaponRecoil_ + kick);
    }
}

void Game::switchWeapon(WeaponType weapon) {
    // Nothing to do if we already hold this weapon (the "Switched to ..." log
    // must not repeat), and never let currentWeapon and currentWeaponInstance
    // drift apart: both are always assigned together in one place below.
    if (currentWeapon == weapon) {
        return;
    }

    currentWeapon = weapon;
    weaponSwitch_ = 1.0f;   // lower the old gun / raise the new one

    // Leaving the sniper cancels a queued bolt rattle from a shot.
    if (weapon != SNIPER) sniperBoltSoundPending_ = false;

    switch (weapon) {
        case PISTOL:
            currentWeaponInstance = pistol;
            std::cout << "Switched to Pistol! Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(pistolAmmo)) << std::endl;
            break;
        case PUMP_SHOTGUN:
            currentWeaponInstance = shotgun;
            std::cout << "Switched to Pump Shotgun! Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(shotgunAmmo)) << std::endl;
            break;
        case SNIPER:
            currentWeaponInstance = sniper;
            // Raising the rifle includes working the bolt. It cannot fire
            // until the rack completes, and the rattle plays shortly after
            // the weapon-switch animation starts.
            sniper->startCycle(kSniperRackTime);
            scheduleSniperBolt(0.18f);
            std::cout << "Switched to Sniper! Racking bolt. Ammo: "
                      << (infiniteAmmoMode ? "∞" : std::to_string(sniper->getAmmo())) << std::endl;
            break;
        case GRENADE:
            currentWeaponInstance = grenade;
            std::cout << "Switched to Grenade! Count: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
            break;
        case FLASHBANG:
            currentWeaponInstance = flashbang;
            std::cout << "Switched to Flashbang! Count: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
            break;
    }
}

void Game::cycleWeapon(int direction) {
    // Mouse-wheel order follows the number keys (1..5). Wrapping keeps the
    // wheel useful in both directions.
    static const WeaponType kOrder[5] = {
        PISTOL, PUMP_SHOTGUN, SNIPER, GRENADE, FLASHBANG
    };
    int index = 0;
    for (int i = 0; i < 5; ++i) {
        if (kOrder[i] == currentWeapon) { index = i; break; }
    }
    index = (index + (direction >= 0 ? 1 : -1) + 5) % 5;
    switchWeapon(kOrder[index]);
}

void Game::updateScope(float deltaTime) {
    // Leaving the match (menu, death screen, editor) ends the scope. Do not
    // touch camera.Zoom outside PLAYING afterwards: other screens own the
    // current FOV.
    if (gameState != PLAYING) {
        if (sniperScoped_) {
            sniperScoped_ = false;
            camera.Zoom = kDefaultFov;
        }
        return;
    }

    // The scope is a held binding while the sniper is the active weapon.
    // The round-freeze releases it like everything else.
    const bool roundIntermission =
        matchOver_ || (roundEndTimer > 0.0f && currentGameMode == BOMB_DEFUSAL);
    const bool canScope =
        window && !spectatorMode && !isDead && !roundIntermission &&
        currentWeapon == SNIPER;

    // The scope is a held binding (right mouse button by default) and can be
    // rebound on the CONTROLS page like every other button.
    const bool scopeDown = canScope && bindingDown(gameSettings_.bindScope);
    const bool wasScoped = sniperScoped_;
    sniperScoped_ = debugScopeOverride_ ? debugScopedValue_ : scopeDown;
    if (sniperScoped_ && !wasScoped) {
        // Scope-in click/zoom sound (2D so it is always audible).
        playUiSound("sniper_scope_in");
    }

    // Smooth FOV transition instead of a hard snap; the blend is frame-rate
    // independent. Mouse look is also slowed down while scoped
    // (see Game::mouseCallback).
    const float targetFov = sniperScoped_ ? kSniperFov : kDefaultFov;
    const float blend = 1.0f - std::exp(-deltaTime * 22.0f);
    camera.Zoom += (targetFov - camera.Zoom) * blend;
    if (std::abs(camera.Zoom - targetFov) < 0.05f) {
        camera.Zoom = targetFov;
    }
}

void Game::scheduleSniperBolt(float delay) {
    sniperBoltSoundPending_ = true;
    sniperBoltSoundDelay_ = std::max(0.0f, delay);
}

void Game::updateSniperBolt(float deltaTime) {
    if (!sniperBoltSoundPending_) return;
    if (gameState != PLAYING) {
        sniperBoltSoundPending_ = false;
        return;
    }
    sniperBoltSoundDelay_ -= deltaTime;
    if (sniperBoltSoundDelay_ > 0.0f) return;

    sniperBoltSoundPending_ = false;
    const float pitch = 0.96f + static_cast<float>(std::rand() % 9) * 0.01f;
    playSoundEffect("sniper_bolt_custom", camera.Position, 0.9f, pitch);
}

void Game::shootPistol() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (pistolCooldown > 0.0f) {
        return;
    }
    
    if (pistolAmmo <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of pistol ammo!" << std::endl;
        return;
    }
    
    createProjectile(camera.Position, camera.Front * 150.0f, 2.0f, 0, 0); // owner=0, type=0 (pistol) for local player
    lastLocalShot_ = std::chrono::steady_clock::now();
    if (!infiniteAmmoMode) {
        pistolAmmo--;
    }
    pistolCooldown = 0.15f; 
    std::cout << "Pistol shot! Active: " << activeProjectiles << " Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(pistolAmmo)) << std::endl;
}

void Game::shootShotgun() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (shotgunAmmo <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of shotgun ammo!" << std::endl;
        return;
    }
    
    // Shoot 5 projectiles in a spread pattern
    int pelletsShot = 0;
    for (int i = 0; i < MAX_PROJECTILES && pelletsShot < 5; i++) {
        if (!projectiles[i].active) {
            // Add spread to shotgun pellets - REDUCED SPREAD
            glm::vec3 spread = glm::vec3(
                (rand() % 100 - 50) * 0.002f, // -0.1 to 0.1 (reduced from 0.5)
                (rand() % 100 - 50) * 0.002f, // -0.1 to 0.1 (reduced from 0.5)
                0.0f
            );
            createProjectile(camera.Position, (camera.Front + spread) * 110.0f, 1.0f, 0, 3); // owner=0, type=3 (shotgun) for local player
            pelletsShot++;
        }
    }
    if (!infiniteAmmoMode) {
        shotgunAmmo--;
    }
    lastLocalShot_ = std::chrono::steady_clock::now();
    shotgunCooldown = 0.8f; // 0.8 second cooldown (pump action)
    std::cout << "Shotgun blast! Pellets: " << pelletsShot << " Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(shotgunAmmo)) << std::endl;
}

void Game::throwGrenade() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (grenadeCount <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of grenades!" << std::endl;
        return;
    }
    
    createProjectile(camera.Position, camera.Front * 30.0f, 5.0f, 0, 1); // owner=0, type=1 (grenade) for local player
    lastLocalShot_ = std::chrono::steady_clock::now();
    if (!infiniteAmmoMode) {
        grenadeCount--;
    }
    grenadeCooldown = 1.5f; // Longer cooldown
    std::cout << "Threw grenade! Grenades left: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
}

void Game::throwFlashbang() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (flashbangCount <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of flashbangs!" << std::endl;
        return;
    }
    
    createProjectile(camera.Position, camera.Front * 25.0f, 4.0f, 0, 2); // owner=0, type=2 (flashbang) for local player
    lastLocalShot_ = std::chrono::steady_clock::now();
    if (!infiniteAmmoMode) {
        flashbangCount--;
    }
    flashbangCooldown = 2.0f; // Longer cooldown
    std::cout << "Threw flashbang! Flashbangs left: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
}

void Game::updateProjectiles(float deltaTime) {
    // Store previous positions for swept collision
    static glm::vec3 prevGrenadePositions[MAX_PROJECTILES];
    
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
            // Store position before update
            glm::vec3 prevPos = projectiles[i].position;
            
            // Apply gravity to all projectiles so they always fall
            projectiles[i].velocity.y -= 15.0f * deltaTime;
            
            // Simple position update
            projectiles[i].position += projectiles[i].velocity * deltaTime;

            // Check projectile type using the type field
            bool isGrenade = projectiles[i].type == 1;
            bool isFlashbang = projectiles[i].type == 2;

            auto detonateAndDeactivate = [&](const glm::vec3& atPos) {
                if (projectiles[i].detonated || !projectiles[i].active) return; // Prevent double detonation
                projectiles[i].detonated = true;
                projectiles[i].active = false;

                if (isGrenade) {
                    createSimpleExplosion(atPos, &projectiles[i]);
                } else if (isFlashbang) {
                    createFlashbangEffect(atPos);
                } else {
                    // Bullets: no explosion, no bounce. Just leave a mark.
                    createBulletHole(atPos, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                activeProjectiles--;
            };
            
            // Check map block collisions for grenades/flashbangs (they should bounce, not stick)
            if ((isGrenade || isFlashbang) && currentMap) {
                const std::vector<MapBlock>& blocks = currentMap->getBlocks();
                glm::vec3 rayDir = projectiles[i].position - prevPos;
                float rayLength = glm::length(rayDir);
                
                if (rayLength > 0.001f) {
                    rayDir = rayDir / rayLength; // Normalize
                }
                
                for (const MapBlock& block : blocks) {
                    if (!block.solid) continue;

                    // Direct hit (slow grenades) respects the block rotation.
                    if (block.containsPoint(projectiles[i].position)) {
                        // No bounce: detonate on impact.
                        detonateAndDeactivate(projectiles[i].position);
                        break;
                    }

                    // Swept collision for fast-moving grenades, against the
                    // real oriented block box.
                    if (rayLength > 0.1f) {
                        const MapBlockRayHit hit =
                            intersectMapBlockRay(block, prevPos, rayDir, rayLength);
                        if (hit.hit) {
                            // Position grenade just before the impact point.
                            projectiles[i].position =
                                prevPos + rayDir * std::max(0.0f, hit.distance - 0.01f);

                            // No bounce: detonate on impact.
                            detonateAndDeactivate(projectiles[i].position);
                            break;
                        }
                    }
                }
                
                // Store current position for next frame
                prevGrenadePositions[i] = projectiles[i].position;
            }
            
            // World boundary: use the loaded map's real extent. The old fixed
            // 20-unit box was inside the walls of the larger maps, so bullets
            // and grenades vanished in mid-air. The +2 margin is only a
            // fallback for maps without solid outer walls.
            const glm::vec3 mapHalf = currentMap ? currentMap->getDimensions() * 0.5f
                                                 : glm::vec3(20.0f);
            const float boundX = mapHalf.x + 2.0f;
            const float boundZ = mapHalf.z + 2.0f;
            const float maxY = std::max(20.0f, mapHalf.y * 2.0f + 5.0f);
            bool hitBoundary =
                projectiles[i].position.x > boundX || projectiles[i].position.x < -boundX ||
                projectiles[i].position.z > boundZ || projectiles[i].position.z < -boundZ ||
                projectiles[i].position.y <= -10.0f || projectiles[i].position.y > maxY;

            if (hitBoundary) {
                // Clamp so effects/holes spawn near the boundary, not far outside it.
                glm::vec3 clamped = projectiles[i].position;
                clamped.x = std::min(boundX, std::max(-boundX, clamped.x));
                clamped.z = std::min(boundZ, std::max(-boundZ, clamped.z));
                clamped.y = std::max(-9.8f, std::min(maxY - 0.1f, clamped.y));

                detonateAndDeactivate(clamped);
                continue;
            }

            // Update lifetime
            projectiles[i].lifetime -= deltaTime;

            // Detonate / expire with no bounce
            if (projectiles[i].lifetime <= 0.0f) {
                // Grenade -> explode, Flashbang -> flash, Bullets -> just disappear.
                if (isGrenade || isFlashbang) {
                    detonateAndDeactivate(projectiles[i].position);
                } else {
                    projectiles[i].active = false;
                    activeProjectiles--;
                }
                continue;
            }

            // Check collision with enemies first
            bool hitTarget = checkProjectileEnemyCollision(projectiles[i]);
            if (projectiles[i].owner == 0 &&
                ((networkClient && networkClient->isConnected()) || localBots_)) {
                // Our own shot: remote bodies are either network-relayed or
                // single-player bots. The hit is resolved on this client
                // (see checkProjectileRemoteCollision).
                hitTarget = checkProjectileRemoteCollision(projectiles[i]) || hitTarget;
            }
            if (hitTarget && projectiles[i].active) {
                // A throwable that hits a body detonates right there (the
                // server only relays blasts that hit the map).
                const bool localThrow =
                    (projectiles[i].type == 1 || projectiles[i].type == 2) && projectiles[i].owner == 0;
                if (localThrow) {
                    projectiles[i].detonated = true;
                    projectiles[i].active = false;
                    activeProjectiles--;
                    if (projectiles[i].type == 1) {
                        // The blast itself reports damage to nearby remote
                        // players (see createSimpleExplosion).
                        createSimpleExplosion(projectiles[i].position, &projectiles[i]);
                    } else {
                        createFlashbangEffect(projectiles[i].position);
                    }
                    std::cout << "Throwable hit a player! Active: " << activeProjectiles << std::endl;
                    continue;
                }

                // Create simple explosion for grenades
                bool isGrenade = projectiles[i].type == 1;
                bool isFlashbang = projectiles[i].type == 2;

                // Every projectile reports itself as the blast source: blasts
                // from other players' relays never damage us from here (see
                // createSimpleExplosion). Local shots reach this branch only
                // against a bot (a remote player hit was handled above).
                if (isGrenade) {
                    if (!projectiles[i].detonated) {
                        projectiles[i].detonated = true;
                        projectiles[i].active = false;
                        activeProjectiles--;
                        createSimpleExplosion(projectiles[i].position, &projectiles[i]);
                    }
                }
                // Create flashbang effect for flashbangs when they expire
                else if (isFlashbang) {
                    if (!projectiles[i].detonated) {
                        projectiles[i].detonated = true;
                        projectiles[i].active = false;
                        activeProjectiles--;
                        createFlashbangEffect(projectiles[i].position);
                    }
                }
                // Deactivate projectile (bullets)
                else {
                    if (projectiles[i].active) {
                        projectiles[i].active = false;
                        activeProjectiles--;
                    }
                }
                std::cout << "Projectile hit enemy! Active: " << activeProjectiles << std::endl;
            }

            // Relayed bullets: stop at our own body. The shooter resolves the
            // damage; this only ends the visible bullet where it hits us.
            if (projectiles[i].active && checkProjectilePlayerBody(projectiles[i])) {
                continue;
            }

            // Check collision with walls (different behavior for different projectile types)
            if (checkProjectileCollision(projectiles[i]) && projectiles[i].active) {
                // Check projectile type by type field
                bool isGrenade = projectiles[i].type == 1;
                bool isFlashbang = projectiles[i].type == 2;
                bool isPistolBullet = projectiles[i].type == 0;
                bool isShotgun = projectiles[i].type == 3;

                if (isPistolBullet || isShotgun) {
                    // Pistol/shotgun bullets: simple damage, no bounce, no explosion
                    playSoundEffect("bullet_impact", projectiles[i].position, 0.6f);
                    createBulletHole(projectiles[i].position, glm::vec3(0.0f, 1.0f, 0.0f)); // Default normal up
                    projectiles[i].active = false;
                    activeProjectiles--;
                    std::cout << "Bullet hit wall! Active: " << activeProjectiles << std::endl;
                }
                else if (isGrenade) {
                    // Grenades: explode on wall hit
                    if (!projectiles[i].detonated) {
                        projectiles[i].detonated = true;
                        projectiles[i].active = false;
                        activeProjectiles--;
                        createSimpleExplosion(projectiles[i].position, &projectiles[i]);
                    }
                    std::cout << "Grenade exploded on wall! Active: " << activeProjectiles << std::endl;
                }
                else if (isFlashbang) {
                    // Flashbangs: flash effect on wall hit
                    if (!projectiles[i].detonated) {
                        projectiles[i].detonated = true;
                        projectiles[i].active = false;
                        activeProjectiles--;
                        createFlashbangEffect(projectiles[i].position);
                    }
                    std::cout << "Flashbang hit wall! Active: " << activeProjectiles << std::endl;
                }
            }
        }
    }
}

void Game::createSimpleExplosion(glm::vec3 position, const Projectile* source) {
    std::cout << "BOOM! Grenade exploded at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;

    // 3D boom at the blast position (attenuated/panned from the camera).
    playSoundEffect("explosion", position);
    
    // Create big red explosion cube that will expand and fade
    // Find an inactive explosion slot
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!explosions[i].active) {
            explosions[i].position = position;
            explosions[i].scale = 0.5f;  // Start small
            explosions[i].maxScale = 3.0f;  // Grow to 3 units
            explosions[i].lifetime = 0.5f;  // 0.5 seconds duration
            explosions[i].active = true;
            explosions[i].color = glm::vec3(1.0f, 0.0f, 0.0f);  // RED
            std::cout << "Explosion effect created! Scale: " << explosions[i].scale << std::endl;
            break;
        }
    }
    
    // Also draw immediate red cube for instant feedback
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, glm::vec3(2.0f)); // Big 2-unit cube
    renderer.drawCube(model, glm::vec3(1.0f, 0.0f, 0.0f)); // RED color for explosion
    
    // Damage nearby enemies. Explosions have no distance falloff: touching the
    // blast (within its radius) always deals the same damage.
    const float explosionRadius = explosionRadius_;
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (enemies[j].active && enemies[j].alive) {
            glm::vec3 enemyFeet(enemies[j].x, enemies[j].y, enemies[j].z);
            float distance = distanceToBody(position, enemyFeet);
            if (distance < explosionRadius) {
                enemies[j].health -= explosionDamage_;
                enemies[j].anim.damage = 1.0f;
                playUiSound("hitmarker");
                std::cout << "Enemy hit by explosion! Health: " << enemies[j].health << std::endl;

                if (enemies[j].health <= 0.0f) {
                    killEnemy(enemies[j]);
                    std::cout << "Enemy killed by explosion!" << std::endl;
                }
            }
        }
    }
    
    // A blast relayed from another player only draws its effect here: the
    // thrower's client is the one that resolves the damage (see below) and
    // reports it to the server.
    const bool localBlast = !source || source->owner == 0;
    if (networkClient && networkClient->isConnected() && !localBlast) {
        return;
    }

    // Damage the local player if the blast reaches the body. No falloff: a hit
    // always deals the same explosion damage (only the hit zone is ignored,
    // because an explosion has no "head shot").
    const float playerDistance = distanceToStanceBody(
        position, playerPos, camera.Yaw, static_cast<uint8_t>(stance_));
    if (playerDistance < explosionRadius) {
        applyPlayerDamage(explosionDamage_);
        damageEffectTime = std::max(damageEffectTime, 1.5f); // Longer screen effect
    }

    // Our own grenade also has to hurt the remote bodies around the blast.
    // Networked clients only see the relayed projectile and deliberately do
    // not apply blast damage themselves (the server is the single authority);
    // offline bots are damaged by the local host.
    if ((networkClient && networkClient->isConnected()) || localBots_) {
        checkExplosionRemoteCollision(position, explosionRadius, explosionDamage_);
    }
}

void Game::createFlashbangEffect(glm::vec3 position) {
    std::cout << "FLASH! Flashbang detonated at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;

    playSoundEffect("flashbang_explode", position);

    // The flash is seen from the eyes, not the feet. Using the feet as the ray
    // origin made a flashbang on lower ground (or on the floor below a ledge)
    // look "blocked" even when the camera could see it directly.
    const glm::vec3 eye = camera.Position;
    float playerDistance = glm::length(position - eye);
    float flashRadius = 12.0f; // 12 unit flash radius (increased for more aggressive effect)

    // Check if there's a wall between the eyes and the flashbang (but ignore
    // walls very close to the flashbang itself).
    bool hasLineOfSight = true;
    if (currentMap) {
        glm::vec3 toFlash = position - eye;
        const float rayLength = glm::length(toFlash);
        glm::vec3 rayDir = rayLength > 0.001f ? toFlash / rayLength : glm::vec3(0.0f);
        const std::vector<MapBlock>& blocks = currentMap->getBlocks();

        for (const MapBlock& block : blocks) {
            if (!block.solid) continue;

            // Oriented block ray cast: a rotated wall blocks the flash where it
            // is drawn, not over the larger axis-aligned box around it.
            const MapBlockRayHit hit =
                intersectMapBlockRay(block, eye, rayDir, rayLength);

            // Only block if the wall is NOT very close to the flashbang
            // (the flashbang may be touching the wall it explodes against).
            if (hit.hit) {
                const float distanceFromFlashbang = rayLength - hit.distance;
                if (distanceFromFlashbang > 0.5f) {
                    hasLineOfSight = false;
                    break;
                }
            }
        }
    }

    if (!hasLineOfSight) {
        std::cout << "Flashbang blocked by wall - no effect" << std::endl;
        return;
    }

    // Calculate facing direction
    glm::vec3 toFlash = position - eye;
    float toLen = glm::length(toFlash);
    if (toLen < 0.001f) toLen = 0.001f;
    toFlash /= toLen;
    float facing = glm::dot(glm::normalize(camera.Front), toFlash); // 1=looking at it, 0=90deg, -1=behind

    // Scale strength by distance
    float flashIntensity = 1.0f - (playerDistance / flashRadius);
    if (flashIntensity < 0.0f) flashIntensity = 0.0f;
    if (flashIntensity > 1.0f) flashIntensity = 1.0f;

    // Calculate duration based on facing and distance
    float duration;
    if (facing > 0.3f) {
        // Looking at flashbang: very aggressive duration
        // Close range: up to 5 seconds, distant: up to 3 seconds
        duration = (5.0f * flashIntensity) + (3.0f * (1.0f - flashIntensity));
        // Add facing bonus
        duration *= (0.5f + facing * 0.5f);
    } else if (facing > 0.0f) {
        // Partially looking: moderate duration
        duration = (3.0f * flashIntensity) + (1.5f * (1.0f - flashIntensity));
    } else {
        // Not looking at it: very short duration but still effective
        duration = 0.5f * flashIntensity;
    }

    // Minimum duration for any flash
    if (duration < 0.2f) duration = 0.2f;

    flashEffectTime = duration;
    isFlashed = true;
    std::cout << "Player flashed! Duration: " << flashEffectTime << " seconds (facing: " << facing << ", distance: " << playerDistance << ")" << std::endl;

    // Disorient enemies temporarily (log-only for now)
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (enemies[j].active && enemies[j].alive) {
            glm::vec3 enemyPos(enemies[j].x, enemies[j].y, enemies[j].z);
            float enemyDistance = glm::length(position - enemyPos);
            if (enemyDistance < flashRadius) {
                std::cout << "Enemy disoriented by flashbang!" << std::endl;
            }
        }
    }
}

void Game::updateFlashbangEffect(float deltaTime) {
    if (flashEffectTime > 0.0f) {
        flashEffectTime -= deltaTime;
        if (flashEffectTime <= 0.0f) {
            isFlashed = false;
            std::cout << "Flash effect worn off" << std::endl;
        }
    }
}

void Game::updateExplosions(float deltaTime) {
    // Update all active explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            // Reduce lifetime
            explosions[i].lifetime -= deltaTime;
            
            // Grow the explosion
            float growthRate = explosions[i].maxScale / 0.5f; // Grow to max in 0.5s
            explosions[i].scale += growthRate * deltaTime;
            
            // Cap at max scale
            if (explosions[i].scale > explosions[i].maxScale) {
                explosions[i].scale = explosions[i].maxScale;
            }
            
            // Deactivate if lifetime expired
            if (explosions[i].lifetime <= 0.0f) {
                explosions[i].active = false;
            }
        }
    }
}

void Game::renderExplosions() {
    // Render all active explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, explosions[i].position);
            model = glm::scale(model, glm::vec3(explosions[i].scale));
            
            // Fade out as lifetime decreases
            float alpha = explosions[i].lifetime / 0.5f;
            if (alpha > 1.0f) alpha = 1.0f;
            if (alpha < 0.0f) alpha = 0.0f;
            
            // Red color with alpha based on lifetime
            glm::vec3 color = explosions[i].color * alpha;
            renderer.drawCube(model, color);
        }
    }
}

bool Game::checkProjectileCollision(Projectile& projectile) {
    // Use the map's real extent (plus a small margin) instead of a fixed box;
    // on the larger maps a fixed boundary cut bullets short in open air.
    const glm::vec3 mapHalf = currentMap ? currentMap->getDimensions() * 0.5f
                                         : glm::vec3(50.0f);
    const float boundX = mapHalf.x + 2.0f;
    const float boundZ = mapHalf.z + 2.0f;
    const float maxY = std::max(20.0f, mapHalf.y * 2.0f + 5.0f);

    // Only check boundaries if projectile has moved significantly from spawn
    float distanceFromPlayer = glm::length(projectile.position - camera.Position);
    if (distanceFromPlayer > 1.0f) { // Only check boundaries after moving 1 unit
        if (std::abs(projectile.position.x) >= boundX ||
            std::abs(projectile.position.z) >= boundZ ||
            projectile.position.y <= -10.0f ||
            projectile.position.y >= maxY) {
            return true;
        }
    }

    // Check collision with map blocks if map is loaded
    if (currentMap && distanceFromPlayer > 0.5f) {
        const std::vector<MapBlock>& blocks = currentMap->getBlocks();

        // Use class-member tracking instead of statics so slot-reuse doesn't
        // cause stale prev-positions from a previous projectile on this slot.
        int projIndex = -1;
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (&projectiles[i] == &projectile) {
                projIndex = i;
                break;
            }
        }

        glm::vec3 prevPos;
        if (projIndex >= 0 && projectilePrevPosInitialized[projIndex]) {
            prevPos = projectilePrevPos[projIndex];
        } else {
            prevPos = projectile.position;
            if (projIndex >= 0) {
                projectilePrevPosInitialized[projIndex] = true;
            }
        }

        glm::vec3 currPos = projectile.position;
        const glm::vec3 segment = currPos - prevPos;
        const float rayLength = glm::length(segment);

        // Swept collision against the blocks' oriented boxes. Only blocks the
        // shot actually crosses are candidates; the nearest impact wins so the
        // bullet stops on the surface instead of at its end-of-frame position
        // (which can be inside or behind the wall). Rotated blocks are tested
        // on their real faces, not their larger axis-aligned bounds.
        if (rayLength > 0.1f) {
            const glm::vec3 rayDir = segment / rayLength;

            float nearestT = std::numeric_limits<float>::infinity();
            glm::vec3 impactPoint = currPos;

            for (const MapBlock& block : blocks) {
                if (!block.solid) continue;

                const MapBlockRayHit hit =
                    intersectMapBlockRay(block, prevPos, rayDir, rayLength);
                if (hit.hit && hit.distance < nearestT) {
                    nearestT = hit.distance;
                    impactPoint = hit.point;
                }
            }

            if (nearestT <= rayLength) {
                projectile.position = impactPoint;
                return true;
            }
        }

        // Store current position for next frame (only if no collision)
        if (projIndex >= 0) {
            projectilePrevPos[projIndex] = currPos;
        }
    }

    return false;
}

void Game::createBulletHole(const glm::vec3& position, const glm::vec3& normal) {
    // Find an inactive bullet hole slot or replace oldest
    int index = -1;
    
    // First, try to find an inactive slot
    for (int i = 0; i < MAX_BULLET_HOLES; i++) {
        if (bulletHoles[i].lifetime <= 0.0f) {
            index = i;
            break;
        }
    }
    
    // If no inactive slot, replace the oldest
    if (index == -1) {
        index = 0;
        for (int i = 0; i < MAX_BULLET_HOLES; i++) {
            if (bulletHoles[i].lifetime < bulletHoles[index].lifetime) {
                index = i;
            }
        }
    }
    
    // Update bullet hole count if we're using a new slot
    if (index >= bulletHoleCount && bulletHoleCount < MAX_BULLET_HOLES) {
        bulletHoleCount = index + 1;
    }
    
    // Create bullet hole
    bulletHoles[index].position = position;
    bulletHoles[index].normal = normal;
    bulletHoles[index].size = 0.2f;
    bulletHoles[index].lifetime = 30.0f; // 30 seconds lifetime
    
    std::cout << "Created bullet hole at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
}

void Game::renderBulletHoles() {
    for (int i = 0; i < bulletHoleCount; i++) {
        if (bulletHoles[i].lifetime > 0.0f) {
            // Create a small dark quad for the bullet hole
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, bulletHoles[i].position);
            model = glm::scale(model, glm::vec3(bulletHoles[i].size, bulletHoles[i].size, bulletHoles[i].size));
            
            // Dark color for bullet hole
            renderer.drawCube(model, glm::vec3(0.1f, 0.1f, 0.1f));
        }
    }
}

void Game::killEnemy(Enemy& enemy) {
    if (!enemy.alive) return;
    // Leave the slot active for a moment so the body can collapse visibly;
    // updateEnemies() retires it after deathTimer reaches zero.
    enemy.alive = false;
    enemy.anim.death = 0.0f;
    enemy.deathTimer = 2.8f;
    kills_++;
    playUiSound("kill_bling");
    std::cout << "Enemy defeated!" << std::endl;
}

void Game::spawnEnemy(const glm::vec3& position) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Spawn with the feet on a real surface: the body model and its
            // hit zones start at the feet, so a target buried in the floor
            // would be invisible and impossible to headshot.
            glm::vec3 feet = position;
            placeAtSafePosition(feet);

            enemies[i].x = feet.x;
            enemies[i].y = feet.y;
            enemies[i].z = feet.z;
            enemies[i].health = 50.0f;
            enemies[i].active = true;
            enemies[i].alive = true;
            enemies[i].yaw = 0.0f;
            enemies[i].deathTimer = 0.0f;
            enemies[i].anim = humanoid::Anim{};
            activeEnemies++;

            std::cout << "Spawned enemy at: (" << enemies[i].x << ", " << enemies[i].y << ", " << enemies[i].z << ")" << std::endl;
            break;
        }
    }

    if (activeEnemies >= MAX_ENEMIES) {
        std::cout << "Max enemies reached!" << std::endl;
    }
}

bool Game::checkProjectileEnemyCollision(Projectile& projectile) {
    // Test the whole step (last checked position -> current position), not just
    // the end point. At high bullet speeds the end point can already be behind
    // the target, which is exactly how hits used to get lost.
    const glm::vec3 from = projectileSegmentStart(projectile);

    int bestIndex = -1;
    ProjectileSweepHit best;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active || !enemies[i].alive) continue;

        // Enemies stand with their feet on their spawn position.
        const glm::vec3 enemyFeet(enemies[i].x, enemies[i].y, enemies[i].z);
        const ProjectileSweepHit hit = sweepProjectileAgainstBody(
            from, projectile.position, enemyFeet, bodyHitSlack);
        if (hit.hit && (!best.hit || hit.t < best.t)) {
            best = hit;
            bestIndex = i;
        }
    }

    if (bestIndex < 0) {
        return false; // No body on the path
    }

    Enemy& target = enemies[bestIndex];
    const glm::vec3 enemyFeet(target.x, target.y, target.z);

    // Stop the shot at the first contact, so sounds, hit zones and impact
    // positions are where the shot actually landed.
    projectile.position = best.point;

    // Grenades/flashbangs do not deal their bullet damage on contact: the
    // throwable branch detonates them, and the blast applies its own fixed
    // damage.
    if (projectile.type == 1 || projectile.type == 2) {
        return true;
    }

    const humanoid::HitZone zone =
        humanoid::hitZone(best.point, enemyFeet, target.yaw);
    const float damage = humanoid::projectileBaseDamage(projectile.type) *
                         humanoid::damageMultiplier(zone);
    target.health -= damage;
    target.anim.damage = 1.0f;
    playSoundEffect("hit_flesh", best.point, 0.8f);
    playUiSound("hitmarker");
    std::cout << "Enemy hit (" << humanoid::hitZoneName(zone) << ", x"
              << humanoid::damageMultiplier(zone) << ")! Health: "
              << target.health << std::endl;

    if (target.health <= 0.0f) {
        killEnemy(target);
    }
    // Bullets stop at the body they hit (throwables are handled above and must
    // stay active until the caller detonates them).
    projectile.active = false;
    activeProjectiles--;
    return true; // Hit detected
}

void Game::checkEnemyPlayerCollision(float deltaTime) {
    if (playerDamageCooldown > 0.0f) {
        playerDamageCooldown -= deltaTime;
        return;
    }
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Check distance between enemy and player
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            float distance = glm::length(enemyPos - playerPos);
            if (distance < 1.5f) { // Damage radius of 1.5 units
                // The target swipes at us: reuse the shooting pose as a punch.
                enemies[i].anim.shoot = 1.0f;
                // Damage player
                playerHealth -= 10.0f; // 10 damage per hit
                playerDamageCooldown = 1.0f; // 1 second cooldown
                damageEffectTime = 0.5f; // 0.5 second screen effect
                std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
                playSoundEffect("player_damage", playerPos + glm::vec3(0.0f, 1.6f, 0.0f), 0.7f);
                
                if (playerHealth <= 0.0f && !isDead) {
                    playerHealth = 0.0f;
                    // One shared death path: it plays the sound, enters
                    // spectator mode in bomb defusal, reports the death to the
                    // server and updates the HUD death counter.
                    dieLocally();
                }
                break; // Only one enemy can damage per frame
            }
        }
    }
}

void Game::updateEnemies(float deltaTime) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;

        if (enemies[i].alive) {
            // Calculate direction to player
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            glm::vec3 direction = playerPos - enemyPos;
            float distance = glm::length(direction);

            float vx = 0.0f, vy = 0.0f, vz = 0.0f;
            if (distance > 0.1f) { // Avoid division by zero
                // Normalize direction and move towards player (2.0 units per second)
                direction = glm::normalize(direction);
                vx = direction.x * 2.0f;
                vy = direction.y * 2.0f;
                vz = direction.z * 2.0f;
                enemies[i].x += vx * deltaTime;
                enemies[i].y += vy * deltaTime;
                enemies[i].z += vz * deltaTime;
                // Face the direction of travel (camera-yaw convention).
                if (std::abs(vx) + std::abs(vz) > 0.01f) {
                    enemies[i].yaw = glm::degrees(std::atan2(vz, vx));
                }
            }
            // Practice targets walk on the floor; the animation still reacts to
            // their own movement speed and direction.
            enemies[i].anim.advance(deltaTime, vx, 0.0f, vz, true);
        } else {
            // Dead: collapse onto the floor, then free the slot.
            enemies[i].anim.death = std::min(1.0f, enemies[i].anim.death + deltaTime * 4.5f);
            enemies[i].deathTimer -= deltaTime;
            if (enemies[i].deathTimer <= 0.0f) {
                enemies[i].active = false;
                enemies[i].alive = false;
                enemies[i].anim.death = 0.0f;
                if (activeEnemies > 0) activeEnemies--;
            }
        }
    }
}

bool Game::checkProjectilePlayerBody(Projectile& projectile) {
    // Hits are resolved once, by the shooter's client, against the same body
    // box and reported to the server. The victim must not apply a second copy
    // locally: that made the victim die on its own while the server (and the
    // other players) still saw a living player.
    if (!networkClient || !networkClient->isConnected()) return false;

    // Only check projectiles from other players (owner != 0 means remote player)
    if (projectile.owner == 0) return false; // Skip own projectiles

    if (projectile.type == 1 || projectile.type == 2) {
        // Relayed grenade/flashbang: the blast is resolved by the thrower
        // (damage) or applies its own local flash effect.
        return false;
    }

    // A relayed bullet ends where it visibly hits this body, but the damage
    // packet comes from the shooter via the server. Sweep the step so a fast
    // relayed bullet cannot fly through us between two frames.
    const glm::vec3 from = projectileSegmentStart(projectile);
    const ProjectileSweepHit hit = sweepProjectileAgainstStanceBody(
        from, projectile.position, playerPos, camera.Yaw, bodyHitSlack,
        static_cast<uint8_t>(stance_));
    if (!hit.hit) return false;

    projectile.position = hit.point;
    projectile.active = false;
    activeProjectiles--;
    return true;
}

void Game::damagePlayer(float damage) {
    if (isDead) return; // Already dead
    
    playerHealth -= damage;
    if (playerHealth < 0.0f) playerHealth = 0.0f;
    
    damageEffectTime = 1.0f; // Screen effect
    playerDamageCooldown = 1.0f; // 1 second immunity
    
    std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
    playSoundEffect("player_damage", playerPos + glm::vec3(0.0f, currentEyeHeight(), 0.0f), 0.9f);
    reportLocalDamage(damage);
    
    if (playerHealth <= 0.0f) {
        dieLocally();
    }
}

void Game::checkPlayerPlayerCollision() {
    if (playerDamageCooldown > 0.0f) {
        return; // Player has damage immunity
    }
    
    // Contact damage is a rate: 15 damage per second of touching, instead of
    // 15 instant damage (which meant one hit per second while standing still).
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        if (!remotePlayers[i].active || remotePlayers[i].isDead) continue;
        
        const float distance = distanceToStanceBody(
            playerPos + glm::vec3(0.0f, currentBodyCenterHeight(), 0.0f),
            remotePlayers[i].position, remotePlayers[i].yaw,
            remotePlayers[i].stance);
        if (distance < 0.5f) {
            // Measured over time: next frame drains the accumulated budget,
            // so a 60 fps game does not take 60 hits at the same spot.
            static float contactBudget = 0.0f;
            contactBudget += playerCollisionDps * (1.0f / 60.0f);
            if (contactBudget >= 1.0f) {
                const float step = std::floor(contactBudget);
                contactBudget -= step;
                damagePlayer(step);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Remote players: our own shots against them, relayed blasts on them, and
// their death state.
//
// The client that shot knows exactly where its shot went, so its own hits are
// authoritative and sent to the server (PACKET_PLAYER_DAMAGE). Everything is
// checked against the same body box the remote player is drawn with, so a hit
// no longer depends on a floating point far above or below the model.
// ---------------------------------------------------------------------------

bool Game::checkProjectileRemoteCollision(Projectile& projectile) {
    // Sweep this frame's whole step so fast shots cannot pass through a body.
    const glm::vec3 from = projectileSegmentStart(projectile);

    RemotePlayer* nearest = nullptr;
    int nearestIndex = -1;
    ProjectileSweepHit best;

    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        RemotePlayer& remote = remotePlayers[i];
        if (!remote.active || remote.isDead) continue;
        if (networkClient && remote.id == networkClient->getClientId()) continue; // never shoot ourselves

        // Remote positions travel as feet positions. The box follows the
        // remote player's stance (standing / crouching / prone) and facing.
        const ProjectileSweepHit hit = sweepProjectileAgainstStanceBody(
            from, projectile.position, remote.position, remote.yaw,
            bodyHitSlack, remote.stance);
        if (!hit.hit) continue;
        if (!best.hit || hit.t < best.t) {
            best = hit;
            nearest = &remote;
            nearestIndex = i;
        }
    }

    if (!nearest) {
        return false;
    }

    // The shot stops at the first body on the path, not at the bullet's
    // end-of-frame position (which may already be behind the target).
    projectile.position = best.point;

    if (projectile.type == 1 || projectile.type == 2) {
        // Our grenade/flashbang hit a body: the thrower resolves the
        // blast here, and the relayed copy (owner != 0) only draws it on
        // the other clients.
        return true;
    }

    // Hit zone: head 2x, torso 1x, arms/legs/feet 0.5x. The multiplier is
    // applied by the shooter, so the server and the victim receive the
    // final damage exactly as resolved here.
    const humanoid::HitZone zone =
        humanoid::hitZone(best.point, nearest->position, nearest->yaw,
                          nearest->stance);
    const float damage = humanoid::projectileBaseDamage(projectile.type) *
                         humanoid::damageMultiplier(zone);

    // A shotgun blast hits one target with several pellets per frame: the
    // pellets are separate damage, not a duplicate hit.
    if (networkClient && networkClient->isConnected()) {
        networkClient->sendPlayerDamage(nearest->id,
                                        static_cast<uint16_t>(damage),
                                        static_cast<uint8_t>(projectile.type));
    } else {
        // Offline bot: the local host owns the body health and respawn.
        damageLocalBot(nearest->id, damage);
    }
    PvpRemote& book = pvpRemotes_[nearestIndex];
    book.lastHitTime = static_cast<float>(glfwGetTime());
    book.health = std::max(0.0f, book.health - damage);
    book.damageSent += static_cast<int>(damage);
    book.pending = true;
    book.sentAt  = std::chrono::steady_clock::now();
    nearest->anim.damage = 1.0f;

    playSoundEffect("hit_flesh", best.point, 0.8f);
    playUiSound("hitmarker");
    if (book.health <= 0.0f && !book.killSoundPlayed) {
        book.killSoundPlayed = true;
        playUiSound("kill_bling");
    }
    std::cout << "Hit remote player " << nearest->id << " (" << humanoid::hitZoneName(zone)
              << ", x" << humanoid::damageMultiplier(zone) << ")"
              << " for " << damage << std::endl;

    projectile.active = false;
    activeProjectiles--;
    return true;
}

void Game::checkExplosionRemoteCollision(const glm::vec3& position, float radius, float damage) {
    if (localBots_) {
        localBots_->explode(position, radius, damage, playerTeam & 1);
        return;
    }
    if (!networkClient || !networkClient->isConnected()) return;

    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        RemotePlayer& remote = remotePlayers[i];
        if (!remote.active || remote.isDead) continue;
        if (remote.id == networkClient->getClientId()) continue;

        // Blasts are measured to the body box (feet position, any height
        // within the stance), so a grenade at the enemy's feet always connects
        // and lying behind cover is harder to hit.
        float distance = distanceToStanceBody(position, remote.position,
                                              remote.yaw, remote.stance);
        if (distance >= radius) continue;

        // Explosions do not fall off with distance: any body the blast reaches
        // takes the full explosion damage.
        const float amount = damage;
        networkClient->sendPlayerDamage(remote.id, static_cast<uint16_t>(amount), 1);
        PvpRemote& book = pvpRemotes_[i];
        book.lastHitTime = static_cast<float>(glfwGetTime());
        book.health = std::max(0.0f, book.health - amount);
        book.damageSent += static_cast<int>(amount);
        book.pending = true;
        book.sentAt  = std::chrono::steady_clock::now();
        playUiSound("hitmarker");
        if (book.health <= 0.0f && !book.killSoundPlayed) {
            book.killSoundPlayed = true;
            playUiSound("kill_bling");
        }
        std::cout << "Blast hit remote player " << remote.id << " for " << amount << std::endl;
    }
}

void Game::updateRemotePlayerDeath(float deltaTime) {
    if (!networkClient || !networkClient->isConnected()) return;

    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        RemotePlayer& remote = remotePlayers[i];
        if (!remote.active) continue;

        // Death and respawn are authoritative on the server: the body is just
        // hidden while isDead, and comes back with the respawn packet.

        PvpRemote& book = pvpRemotes_[i];
        if (!book.pending) continue;

        const float since = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - book.sentAt).count();

        if (remote.isDead) {
            // The server has killed this player: everything we claimed is
            // settled. If one of our hits landed shortly before the death,
            // credit the kill on the local HUD (the server has no scoreboard
            // yet; this is the shooter's view of the exchange).
            const float sinceHit = static_cast<float>(glfwGetTime()) - book.lastHitTime;
            if (book.lastHitTime > 0.0f && sinceHit >= 0.0f && sinceHit < 3.0f) {
                kills_++;
                if (currentGameMode == TEAM_VS_TEAM &&
                    remote.team != static_cast<uint8_t>(playerTeam & 1)) {
                    if (playerTeam == 0) ++teamAScore_;
                    else                 ++teamBScore_;
                }
                if (!book.killSoundPlayed) {
                    playUiSound("kill_bling");
                }
                checkMatchEnd();
                std::cout << "Player " << remote.id << " eliminated - kill credited" << std::endl;
            }
            book = PvpRemote{};
            continue;
        }

        // No authoritative state after the hit: the packet (or its relay)
        // was lost. Re-send the damage we reported, so hits are not silently
        // dropped.
        if (since > pvpReportReconcileWindow) {
            const int missing = std::min(book.damageSent, 100);
            if (missing > 0) {
                std::cout << "Re-sending " << missing << " damage for player "
                          << remote.id << std::endl;
                networkClient->sendPlayerDamage(remote.id, static_cast<uint16_t>(missing), 0);
            }
            book.pending = false;
        }
    }
}

void Game::updateRemoteAnimations(float deltaTime) {
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        RemotePlayer& remote = remotePlayers[i];
        if (!remote.active) continue;

        if (remote.isDead) {
            // Advance the collapse; dead bodies stay on the map until the
            // server respawns the player at the next round.
            remote.anim.death = std::min(1.0f, remote.anim.death + deltaTime * 4.5f);
            remote.anim.advance(deltaTime, 0.0f, 0.0f, 0.0f, true);
            continue;
        }
        remote.anim.death = 0.0f;

        // Positions arrive at the packet rate, not every frame. A change in
        // height marks a jump/fall and keeps the air pose alive briefly, so
        // walking on an elevated platform is not mistaken for falling (the
        // server's velY is not reliable ground information there).
        if (!remote.anim.hasLastY) {
            remote.anim.lastY = remote.position.y;
            remote.anim.hasLastY = true;
        }
        const float dy = remote.position.y - remote.anim.lastY;
        remote.anim.lastY = remote.position.y;
        if (std::abs(dy) > 0.002f) {
            remote.anim.airHold = 0.25f;
        } else {
            remote.anim.airHold = std::max(0.0f, remote.anim.airHold - deltaTime);
        }
        const bool grounded = remote.anim.airHold <= 0.0f;
        remote.anim.advance(deltaTime, remote.velocity.x, remote.velocity.y,
                            remote.velocity.z, grounded);
    }
}

void Game::renderRemotePlayers() {
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        RemotePlayer& remote = remotePlayers[i];
        // Dead players are rendered too: their body collapses and stays on
        // the ground until the server respawns them next round.
        if (!remote.active) continue;

        // Smooth out the network rate: converge on the latest position. The
        // height follows exactly (a body never slides vertically between the
        // floor it stands on and the one behind), only X/Z are smoothed: a
        // stale height made the remote body float while its hits used the
        // real one.
        if (glm::length(remote.renderPosition - glm::vec3(0.0f)) < 0.001f) {
            remote.renderPosition = remote.position;
        } else {
            const float interpolationFactor = 0.2f;
            remote.renderPosition.x += (remote.position.x - remote.renderPosition.x) * interpolationFactor;
            remote.renderPosition.z += (remote.position.z - remote.renderPosition.z) * interpolationFactor;
            remote.renderPosition.y = remote.position.y;
        }

        // Team modes: teammates are blue, enemies red. Free-for-all modes
        // have no teammates, so everybody else is red.
        const bool teamMode = (currentGameMode == TEAM_VS_TEAM ||
                               currentGameMode == BOMB_DEFUSAL);
        const bool teammate = teamMode && (remote.team == playerTeam);
        const glm::vec3 color = teammate
                              ? glm::vec3(0.15f, 0.4f, 1.0f)   // blue
                              : glm::vec3(1.0f, 0.15f, 0.15f); // red

        const float healthPct = remote.isDead
                                    ? 0.0f
                                    : glm::clamp(remote.health / 100.0f, 0.0f, 1.0f);
        const uint8_t renderStance = remote.isDead ? 0 : remote.stance;
        if (playerModel_ && remoteModelAnims[i].valid()) {
            posePlayerInstance(remoteModelAnims[i], remote.anim);
            renderer.submitModel(playerModel_.get(), &remoteModelAnims[i],
                                 humanoid::rootTransform(remote.renderPosition, remote.yaw,
                                                         renderStance),
                                 color);
        } else {
            humanoid::render(renderer, remote.renderPosition, remote.yaw, color,
                             healthPct, remote.anim, remote.pitch, !remote.isDead,
                             renderStance);
        }
    }
}

// Bomb Defusal Mode Functions
void Game::updateBomb(float deltaTime) {
    if (!bombDefusalMode) return;

    // Elapsed round time drives the HUD in local play (and the server-owned
    // path keeps it in sync as a fallback between state packets).
    if (roundActive) roundTime += deltaTime;

    // Multiplayer: the dedicated server owns planting, defusing, the fuse and
    // the round result (see ServerBombManager). The client only advances the
    // visual countdown between state packets and plays the beeps; it must not
    // plant/defuse/explode locally or clients would disagree.
    const bool serverOwned = networkClient && networkClient->isConnected();
    if (serverOwned) {
        if (roundActive) {
            // The server broadcasts the authoritative clock; interpolate
            // between packets for a smooth HUD. Once planted, the bomb timer
            // replaces it.
            if (!bomb.planted) {
                roundTimeLeft = std::max(0.0f, roundTimeLeft - deltaTime);
            }
        }
        if (bomb.planted && !bomb.defused && roundActive) {
            const float before = bomb.plantTime;
            bomb.plantTime += deltaTime;
            if (bomb.plantTime > bomb.explodeTime) bomb.plantTime = bomb.explodeTime;
            const float remaining = bomb.explodeTime - bomb.plantTime;
            const float beepInterval = remaining <= 5.0f ? 0.5f : 1.0f;
            if (std::floor(before / beepInterval) != std::floor(bomb.plantTime / beepInterval)) {
                playSoundEffect("bomb_beep", bomb.position, remaining <= 5.0f ? 1.0f : 0.7f);
            }
        }

        // UDP is lossy: while an interaction is active keep telling the
        // server so a dropped request cannot leave the player "planting"
        // forever on their own screen. The server expires the intent when
        // these stop, so releasing E also cancels it.
        if (networkClient->isConnected() && (isPlanting || isDefusing)) {
            bombInteractResend_ -= deltaTime;
            if (bombInteractResend_ <= 0.0f) {
                bombInteractResend_ = 0.2f;
                networkClient->sendBombInteract(isPlanting ? BOMB_INTERACT_PLANT
                                                           : BOMB_INTERACT_DEFUSE);
            }
        } else {
            bombInteractResend_ = 0.0f;
        }
        return;
    }

    // Offline round clock: defenders win when it expires before a plant.
    if (roundActive && !bomb.planted) {
        roundTimeLeft = std::max(0.0f, roundTimeLeft - deltaTime);
        if (roundTimeLeft <= 0.0f) {
            endRound(1 - attackingTeam, ROUND_END_TIME_EXPIRED);
        }
    }
    
    // Update bomb timer if planted
    if (bomb.planted && !bomb.defused) {
        const float before = bomb.plantTime;
        bomb.plantTime += deltaTime;

        // Countdown beeps: once per second, twice as fast in the final 5 s.
        const float remaining = bomb.explodeTime - bomb.plantTime;
        const float beepInterval = remaining <= 5.0f ? 0.5f : 1.0f;
        if (std::floor(before / beepInterval) != std::floor(bomb.plantTime / beepInterval)) {
            playSoundEffect("bomb_beep", bomb.position, remaining <= 5.0f ? 1.0f : 0.7f);
        }
        
        // Check if bomb exploded
        if (bomb.plantTime >= bomb.explodeTime) {
            std::cout << "BOMB EXPLODED!" << std::endl;
            // The round-ending detonation is played by endRound() below so
            // every client (including multiplayer) hears the same echo.
            bomb.active = false;
            bomb.planted = false; // Prevent re-triggering
            endRound(bomb.plantingTeam, ROUND_END_BOMB_EXPLODED); // Attackers win
        }
    }
    
    // A round that just ended (time-out or detonation) must not still accept
    // a plant/defuse finish in the same frame.
    if (!roundActive) return;

    // Update planting progress
    if (isPlanting) {
        plantProgress += deltaTime;
        if (plantProgress >= 3.0f) { // 3 seconds to plant
            plantProgress = 0.0f;
            isPlanting = false;
            
            // Plant the bomb
            int siteIndex = getNearbyBombSite();
            if (siteIndex >= 0) {
                bomb.position = bombSites[siteIndex].position;
                bomb.planted = true;
                bomb.active = true;
                bomb.plantTime = 0.0f;
                bomb.plantingTeam = playerTeam;
                bomb.defused = false;
                playSoundEffect("bomb_plant", bomb.position);
                std::cout << "BOMB PLANTED by Team " << playerTeam << "!" << std::endl;
            }
        }
    }
    
    // Update defusing progress
    if (isDefusing) {
        defuseProgress += deltaTime;
        if (defuseProgress >= 4.0f) { // 4 seconds to defuse
            defuseProgress = 0.0f;
            isDefusing = false;
            
            // Defuse the bomb
            if (bomb.planted && !bomb.defused) {
                bomb.defused = true;
                bomb.active = false;
                playSoundEffect("bomb_defuse", bomb.position);
                std::cout << "BOMB DEFUSED!" << std::endl;
                endRound(1 - bomb.plantingTeam, ROUND_END_BOMB_DEFUSED); // Defenders win
            }
        }
    }

    if (roundActive) checkLocalBombRoundOutcome();
}

void Game::checkLocalBombRoundOutcome() {
    if (!bombDefusalMode || !roundActive || bomb.defused) return;

    // Count every participant on each team: the local player and the offline
    // bots. A team with no participants at all must not instantly lose.
    int present[2] = {0, 0};
    int alive[2] = {0, 0};

    const int localTeam = playerTeam & 1;
    present[localTeam] = 1;
    if (!isDead && !spectatorMode) alive[localTeam] = 1;

    if (localBots_) {
        for (const LocalBotHost::View& view : localBots_->views()) {
            const int team = view.team & 1;
            ++present[team];
            if (!view.isDead) ++alive[team];
        }
    }

    const int attackers = attackingTeam & 1;
    const int defenders = 1 - attackers;

    // Defenders eliminated: attackers win, with or without the bomb planted.
    if (present[defenders] > 0 && alive[defenders] == 0) {
        endRound(attackers, ROUND_END_DEFENDERS_ELIMINATED);
        return;
    }

    // Attackers eliminated before a plant: defenders win. After a plant the
    // defenders still have to defuse, exactly like the server rules.
    if (!bomb.planted && present[attackers] > 0 && alive[attackers] == 0) {
        endRound(defenders, ROUND_END_ATTACKERS_ELIMINATED);
    }
}

void Game::startPlanting() {
    if (!bombDefusalMode || bomb.planted || isPlanting) return;
    
    // Only attacking team can plant
    if (playerTeam != attackingTeam) return;
    
    int siteIndex = getNearbyBombSite();
    if (siteIndex >= 0) {
        isPlanting = true;
        plantProgress = 0.0f;
        playSoundEffect("bomb_plant", playerPos);
        // The server validates the site/team again and advances the timer.
        if (networkClient && networkClient->isConnected()) {
            networkClient->sendBombInteract(BOMB_INTERACT_PLANT);
        }
    }
}

void Game::startDefusing() {
    if (!bombDefusalMode || !bomb.planted || bomb.defused || isDefusing) return;
    
    // Only defending team can defuse
    if (playerTeam == bomb.plantingTeam) return;
    
    if (isNearBomb()) {
        isDefusing = true;
        defuseProgress = 0.0f;
        playSoundEffect("bomb_defuse", playerPos);
        if (networkClient && networkClient->isConnected()) {
            networkClient->sendBombInteract(BOMB_INTERACT_DEFUSE);
        }
    }
}

int Game::getNearbyBombSite() {
    // Match the server's use range (ServerBombManager::Config::useRange).
    // Horizontal distance is what matters; a site on a small platform must
    // still be usable when the player stands at a slightly different height.
    constexpr float kUseRange = 2.5f;
    constexpr float kUseHeight = 2.5f;
    for (int i = 0; i < activeBombSites; i++) {
        if (!bombSites[i].active) continue;

        const glm::vec3 delta = playerPos - bombSites[i].position;
        const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        if (horizontal < kUseRange && std::abs(delta.y) < kUseHeight) {
            return i;
        }
    }
    return -1;
}

bool Game::isNearBomb() {
    if (!bomb.planted || bomb.defused || !bomb.active) return false;

    constexpr float kUseRange = 2.5f;
    constexpr float kUseHeight = 2.5f;
    const glm::vec3 delta = playerPos - bomb.position;
    const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    return horizontal < kUseRange && std::abs(delta.y) < kUseHeight;
}

void Game::endRound(int winner, RoundEndReason reason) {
    // Prevent re-entry if round already ended
    if (roundEndTimer > 0.0f) return;
    
    roundActive = false;
    roundWinner = winner;
    roundEndReason = reason;
    // CS-style post-round window: players can keep moving for a few seconds
    // while the result popup is up, then the next round starts. Bomb defusal
    // uses the short 3 s window requested for the blast result.
    roundEndTimer = (currentGameMode == BOMB_DEFUSAL) ? 3.0f : 5.0f;
    if (winner == 0)      teamAScore_++;
    else if (winner == 1) teamBScore_++;
    playUiSound("round_end");
    
    if (winner == 0) {
        std::cout << "ROUND OVER: Team A (Red) wins!" << std::endl;
    } else if (winner == 1) {
        std::cout << "ROUND OVER: Team B (Blue) wins!" << std::endl;
    } else {
        std::cout << "ROUND OVER: Draw!" << std::endl;
    }
    
    // Side switch schedule. Free play keeps the old round-by-round swap;
    // normal games switch once after round 12, swift games after round 6.
    bool swapSides = true;
    if (currentGameMode == BOMB_DEFUSAL && matchFormat_ != MATCH_FREE_PLAY) {
        const int played = roundNumber_;
        if (matchFormat_ == MATCH_NORMAL) {
            swapSides = (played == 12);
        } else if (matchFormat_ == MATCH_SWIFT) {
            swapSides = (played == 6);
        }
    }
    if (swapSides) attackingTeam = 1 - attackingTeam;

    // A round win can finish the match (13/7 round formats).
    checkMatchEnd();
    
    // Create a big explosion only when the bomb actually detonated. A defuse
    // or an elimination round end while the bomb is planted must not look
    // like a detonation.
    const bool bombExploded = reason == ROUND_END_BOMB_EXPLODED ||
                              (reason == ROUND_END_NONE && (bomb.planted || bomb.active));
    if (bombExploded) {
        // 2D/global sound: the detonation is heard across the whole map, not
        // attenuated by the listener's distance, and carries the echo baked
        // into bomb_explode_echo.wav.
        playUiSound("bomb_explode_echo");
    }
    if (bombExploded) {
        // Create a massive explosion
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].active) {
                explosions[i].position = bomb.position;
                explosions[i].scale = 0.5f;
                explosions[i].maxScale = 8.0f;  // Huge explosion!
                explosions[i].lifetime = 2.0f;  // 2 seconds duration
                explosions[i].active = true;
                explosions[i].color = glm::vec3(1.0f, 0.6f, 0.0f);  // Orange-red
                break;
            }
        }
        // Create a second overlapping explosion for more effect
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].active) {
                explosions[i].position = bomb.position + glm::vec3(1.0f, 0.0f, 1.0f);
                explosions[i].scale = 0.3f;
                explosions[i].maxScale = 6.0f;
                explosions[i].lifetime = 1.5f;
                explosions[i].active = true;
                explosions[i].color = glm::vec3(1.0f, 0.2f, 0.0f);  // Red
                break;
            }
        }
        // Third explosion for dramatic effect
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].active) {
                explosions[i].position = bomb.position + glm::vec3(-1.0f, 0.5f, -0.5f);
                explosions[i].scale = 0.4f;
                explosions[i].maxScale = 5.0f;
                explosions[i].lifetime = 1.8f;
                explosions[i].active = true;
                explosions[i].color = glm::vec3(1.0f, 0.8f, 0.0f);  // Yellow
                break;
            }
        }
    }
    
    // Also damage/kill the player if near the bomb explosion. In multiplayer
    // the server applies the blast to everyone (including us) and reports the
    // authoritative health; applying it here as well would double the damage.
    const bool serverOwnedBlast = networkClient && networkClient->isConnected();
    if (!serverOwnedBlast && bombExploded) {
        float distToBomb = glm::length(playerPos - bomb.position);
        if (distToBomb < 12.0f) {
            // A blast that reaches the body always deals the full damage; it
            // does not taper off towards the edge of the radius.
            damagePlayer(100.0f);
        }
    }
}

void Game::updateRoundEndTimer(float deltaTime) {
    if (roundEndTimer > 0.0f) {
        roundEndTimer -= deltaTime;
        if (roundEndTimer <= 0.0f) {
            if (!matchOver_) startRound();
        }
    }
}

void Game::startRound() {
    roundActive = true;
    roundTime = 0.0f;
    roundTimeLeft = kLocalBombRoundTime;
    roundNumber_++;
    teamADeaths = 0;
    teamBDeaths = 0;
    
    // Reset bomb
    bomb.planted = false;
    bomb.defused = false;
    bomb.active = false;
    bomb.plantTime = 0.0f;
    serverPlantActor_ = 0;
    serverDefuseActor_ = 0;
    
    // Reset players
    resetGame();

    // Single-player bomb defusal: bots that died in the previous round come
    // back at the same time as the player.
    if (localBots_) localBots_->startNewRound(attackingTeam);

    // Round-based multiplayer: tell the server so it revives the bots together
    // with the human players at the start of the next round.
    if (networkClient && networkClient->isConnected()) {
        networkClient->sendRoundStart(static_cast<uint8_t>(currentGameMode),
                                      static_cast<uint8_t>(attackingTeam & 1),
                                      static_cast<uint8_t>(matchFormat_));
    }

    playUiSound("round_start");
    std::cout << "NEW ROUND STARTED! Attacking Team: " << (attackingTeam == 0 ? "Team A (Red)" : "Team B (Blue)") << std::endl;
}

void Game::updateSpectator(float deltaTime) {
    if (!spectatorMode) return;

    // Free-flying camera, like the map editor: no gravity, no collision.
    // A spectator who died should be able to look around the map while the
    // round continues, not fall to the floor as a body.
    const float spectatorSpeed = 15.0f;
    glm::vec3 moveDir(0.0f);

    if (keyDown(keyBindings.forward))  moveDir += camera.Front;
    if (keyDown(keyBindings.backward)) moveDir -= camera.Front;
    if (keyDown(keyBindings.left))     moveDir -= camera.Right;
    if (keyDown(keyBindings.right))    moveDir += camera.Right;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)       moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

    if (glm::length(moveDir) > 0.0001f) {
        moveDir = glm::normalize(moveDir);
        camera.Position += moveDir * spectatorSpeed * deltaTime;
    }

    // Keep the stored body position in sync with the free camera. It is not
    // used for collision (the body is dead) but keeps the network input
    // interesting position sensible.
    playerPos = camera.Position - glm::vec3(0.0f, currentEyeHeight(), 0.0f);
    playerVelocity = glm::vec3(0.0f);
    camera.updateCameraVectors();
}

// ---------------------------------------------------------------------------
// Single-player bots
// ---------------------------------------------------------------------------

int Game::debugLocalBotCount() const {
    return localBots_ ? localBots_->botCount() : 0;
}

int Game::setupBotsTeamA() const {
    // Deathmatch has a single bot pool; the bot AI ignores team sides in that
    // mode, so all of them go on side A.
    return matchUsesTeamScore() ? std::max(0, botSetupTeamA_)
                                : std::max(0, botSetupBots_);
}

int Game::setupBotsTeamB() const {
    return matchUsesTeamScore() ? std::max(0, botSetupTeamB_) : 0;
}

void Game::startLocalBots() {
    stopLocalBots();
    if (multiplayerMode || activeMapFile_.empty()) return;
    if (setupBotsTeamA() <= 0 && setupBotsTeamB() <= 0) return;

    LocalBotHost::Config config;
    config.mapPath = "assets/maps/" + activeMapFile_;
    config.mapName = activeMapFile_;
    config.gameMode = static_cast<int>(currentGameMode);
    config.matchFormat = static_cast<int>(matchFormat_);
    config.attackingTeam = attackingTeam & 1;
    config.botsTeamA = setupBotsTeamA();
    config.botsTeamB = setupBotsTeamB();
    config.accuracy = static_cast<float>(botSetupAccuracy_) / 100.0f;
    config.friendlyFire = false;

    auto host = std::make_unique<LocalBotHost>();
    if (!host->init(config)) {
        std::cerr << "[LocalBots] Init failed" << std::endl;
        return;
    }
    localBots_ = std::move(host);
}

void Game::stopLocalBots() {
    if (!localBots_) return;
    const uint32_t firstId = LocalBotHost::kFirstBotId;
    for (int i = 0; i < MAX_REMOTE_PLAYERS; ++i) {
        if (remotePlayers[i].active && remotePlayers[i].id >= firstId) {
            remotePlayers[i].active = false;
            remotePlayers[i].isDead = true;
            pvpRemotes_[i] = PvpRemote{};
        }
    }
    localBots_.reset();
}

void Game::updateLocalBots(float deltaTime) {
    if (!localBots_) return;

    const bool playerAlive = !isDead && !spectatorMode && gameState == PLAYING;
    localBots_->update(
        deltaTime, playerPos, playerTeam & 1, playerAlive,
        [this](float damage) { damagePlayer(damage); },
        [this](const glm::vec3& origin, const glm::vec3& velocity, uint8_t type) {
            // Visual only: the host already applied the hit. A non-zero owner
            // makes the normal player-shot collision path ignore it.
            createProjectile(origin, velocity, 1.2f, 1u, static_cast<int>(type));
        });

    // Copy bodies into the existing remote-player slots so rendering,
    // hit detection and animations work exactly like networked players.
    const std::vector<LocalBotHost::View> views = localBots_->views();
    for (int i = 0; i < MAX_REMOTE_PLAYERS; ++i) {
        if (i >= static_cast<int>(views.size())) {
            if (remotePlayers[i].active && remotePlayers[i].id >= LocalBotHost::kFirstBotId) {
                remotePlayers[i].active = false;
                remotePlayers[i].isDead = true;
                pvpRemotes_[i] = PvpRemote{};
            }
            continue;
        }

        const LocalBotHost::View& view = views[static_cast<size_t>(i)];
        const bool isNew = !remotePlayers[i].active || remotePlayers[i].id != view.id;
        const bool respawned = !isNew && remotePlayers[i].isDead && !view.isDead;
        if (isNew || respawned) {
            remotePlayers[i].anim = humanoid::Anim{};
            pvpRemotes_[i] = PvpRemote{};
            remotePlayers[i].renderPosition = view.position;
        }
        remotePlayers[i].id       = view.id;
        remotePlayers[i].position = view.position;
        remotePlayers[i].velocity = view.velocity;
        remotePlayers[i].yaw      = view.yaw;
        remotePlayers[i].pitch    = view.pitch;
        remotePlayers[i].health   = view.health;
        remotePlayers[i].isDead   = view.isDead;
        remotePlayers[i].active   = true;
        remotePlayers[i].team     = view.team;
        remotePlayers[i].stance   = 0;   // bot AI never crouches or goes prone
        if (view.shooting) remotePlayers[i].anim.shoot = 1.0f;
    }

    // The host is the scoreboard in single player.
    kills_ = localBots_->playerKills();
    if (currentGameMode == TEAM_VS_TEAM) {
        teamAScore_ = localBots_->teamAKills();
        teamBScore_ = localBots_->teamBKills();
    }
    checkMatchEnd();
}

bool Game::damageLocalBot(uint32_t id, float damage) {
    if (!localBots_) return false;
    const bool hit = localBots_->damageBot(id, damage, playerTeam & 1);
    if (hit) {
        kills_ = localBots_->playerKills();
        if (currentGameMode == TEAM_VS_TEAM) {
            teamAScore_ = localBots_->teamAKills();
            teamBScore_ = localBots_->teamBKills();
        }
        checkMatchEnd();
    }
    return hit;
}

// ---------------------------------------------------------------------------
// Match length / win conditions
// ---------------------------------------------------------------------------

int Game::matchKillTarget() const {
    if (matchFormat_ == MATCH_NORMAL) return 50;
    if (matchFormat_ == MATCH_SWIFT) return 25;
    return 0;
}

int Game::matchRoundTarget() const {
    if (matchFormat_ == MATCH_NORMAL) return 13;
    if (matchFormat_ == MATCH_SWIFT) return 7;
    return 0;
}

bool Game::matchUsesTeamScore() const {
    return currentGameMode == TEAM_VS_TEAM || currentGameMode == BOMB_DEFUSAL;
}

void Game::checkMatchEnd() {
    if (matchOver_ || matchFormat_ == MATCH_FREE_PLAY) return;

    if (currentGameMode == BOMB_DEFUSAL) {
        // First to the format's round target wins (13 normal, 7 swift).
        // There is no win-by-two overtime: 12-12 is decided by the next round.
        const int target = matchRoundTarget();
        const int best = std::max(teamAScore_, teamBScore_);
        if (best < target) return;
        matchOver_ = true;
        matchWinner_ = teamAScore_ > teamBScore_ ? 0 : 1;
        playUiSound("round_end");
        std::cout << "MATCH OVER: Team " << (matchWinner_ == 0 ? "A" : "B")
                  << " wins " << teamAScore_ << ":" << teamBScore_ << std::endl;
        return;
    }

    const int target = matchKillTarget();
    if (target <= 0) return;

    if (currentGameMode == TEAM_VS_TEAM) {
        if (teamAScore_ < target && teamBScore_ < target) return;
        matchOver_ = true;
        matchWinner_ = teamAScore_ >= target ? 0 : 1;
    } else {
        if (kills_ < target) return;
        matchOver_ = true;
        matchWinner_ = 2;   // the local player
    }
    playUiSound("round_end");
    std::cout << "MATCH OVER: " << (matchWinner_ == 2
                                        ? std::string("player")
                                        : "Team " + std::string(matchWinner_ == 0 ? "A" : "B"))
              << " reached " << target << " kills" << std::endl;
}

void Game::restartMatch() {
    matchOver_ = false;
    matchWinner_ = -1;
    attackingTeam = 0;
    roundNumber_ = 1;
    teamAScore_ = 0;
    teamBScore_ = 0;
    refreshBombSitesForMode();
    resetGame();
    if (!multiplayerMode && (setupBotsTeamA() > 0 || setupBotsTeamB() > 0)) {
        startLocalBots();
    }
}

