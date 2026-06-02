#include "game.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>
#include <GLFW/glfw3.h>

void Game::updatePlayerPhysics(float deltaTime) {
    // AUTOMATIC UNSTUCK: Check if player is inside a block and move them out
    if (currentMap && currentMap->checkCollision(playerPos, 0.15f)) {
        int unstuckCounter = 0;
        while (currentMap->checkCollision(playerPos, 0.15f) && unstuckCounter < 50) {
            playerPos.y += 0.2f; // Move up gradually
            unstuckCounter++;
        }
        if (unstuckCounter > 0) {
            std::cout << "UNSTUCK: Moved player up by " << (unstuckCounter * 0.2f) << " units" << std::endl;
            playerVelocity = glm::vec3(0.0f, 0.0f, 0.0f); // Reset velocity
        }
    }
    
    // Check for death by falling
    if (playerPos.y < -20.0f && !isDead) {
        isDead = true;
        playerHealth = 0.0f;
        
        if (bombDefusalMode) {
            spectatorMode = true;
            std::cout << "Player died! Entering spectator mode." << std::endl;
        } else {
            gameState = DEAD;
        }
        std::cout << "Player fell out of the world and died!" << std::endl;
        return;
    }
    
    // Update jump cooldown
    if (jumpCooldown > 0.0f) {
        jumpCooldown -= deltaTime;
    }
    
    // Apply gravity
    playerVelocity.y -= 9.8f * deltaTime;
    
    // Calculate intended new position
    glm::vec3 newPos = playerPos + playerVelocity * deltaTime;
    
    // Store original position
    glm::vec3 originalPos = playerPos;
    
    // Check collision at new position
    bool collision = false;
    if (currentMap) {
        collision = currentMap->checkCollision(newPos, 0.2f); // Smaller collision radius for accurate hit detection
    }

    if (collision) {
        // Try X axis movement only
        glm::vec3 testPosX = originalPos;
        testPosX.x = newPos.x;
        bool xCollision = currentMap->checkCollision(testPosX, 0.2f);

        // Try Z axis movement only
        glm::vec3 testPosZ = originalPos;
        testPosZ.z = newPos.z;
        bool zCollision = currentMap->checkCollision(testPosZ, 0.2f);

        // Apply horizontal movement first (walls should stop horizontal movement)
        if (!xCollision) {
            playerPos.x = newPos.x;
        } else {
            playerVelocity.x = 0.0f;
        }

        if (!zCollision) {
            playerPos.z = newPos.z;
        } else {
            playerVelocity.z = 0.0f;
        }

        // Get ground information at the new horizontal position for slope handling
        float groundHeight;
        glm::vec3 groundNormal;
        bool foundGround = currentMap->getGroundAt(playerPos, 0.2f, groundHeight, groundNormal);

        // If on a slope, adjust Y position to follow the slope when walking
        if (foundGround && (playerVelocity.x != 0.0f || playerVelocity.z != 0.0f)) {
            float slopeStrength = 1.0f - groundNormal.y;
            if (slopeStrength > 0.0f) {
                // Calculate target Y based on ground height at current position
                float targetY = groundHeight + 1.6f;
                if (targetY > playerPos.y && targetY - playerPos.y < 0.5f) {
                    // Allow walking up the slope
                    playerPos.y = targetY;
                    playerVelocity.y = 0.0f;
                }
            }
        }

        // Now check Y axis movement only if horizontal movement was successful
        // This prevents getting pushed up when hitting walls
        glm::vec3 testPosY = playerPos; // Use updated position after horizontal movement
        testPosY.y = newPos.y;
        bool yCollision = currentMap->checkCollision(testPosY, 0.2f);

        if (!yCollision) {
            playerPos.y = newPos.y;
        } else {
            // Check if we're hitting a floor (moving down and blocked)
            if (playerVelocity.y < 0.0f) {
                playerVelocity.y = 0.0f;
                isJumping = false;

                // If on a slope, add some velocity to slide down
                if (foundGround && groundNormal.y < 0.95f) {
                    float slopeStrength = 1.0f - groundNormal.y;
                    playerVelocity.x -= groundNormal.x * slopeStrength * 5.0f;
                    playerVelocity.z -= groundNormal.z * slopeStrength * 5.0f;
                }
            } else if (playerVelocity.y > 0.0f) {
                // Hitting ceiling
                playerVelocity.y = 0.0f;
            }
        }
    } else {
        // No collision - allow full movement (falling through air)
        playerPos = newPos;
    }
    
    // Ground detection - check if we're standing on a solid block
    bool onGround = false;
    if (currentMap) {
        // Check if standing on a block - use the new getGroundAt function for rotated blocks
        float groundHeight;
        glm::vec3 groundNormal;
        bool foundGround = currentMap->getGroundAt(playerPos, 0.2f, groundHeight, groundNormal);

        if (foundGround) {
            // Check if player is close enough to the ground
            float heightAboveGround = playerPos.y - groundHeight;
            if (heightAboveGround < 1.7f && heightAboveGround > 0.0f) {
                onGround = true;

                // Snap to ground height if falling
                if (playerVelocity.y <= 0.0f) {
                    playerPos.y = groundHeight + 1.6f; // Eye level when on ground
                    playerVelocity.y = 0.0f;
                    isJumping = false;

                    // Adjust velocity based on ground normal (for walking on slopes)
                    float slopeFactor = 1.0f - groundNormal.y;
                    if (slopeFactor > 0.0f) {
                        // Add a small upward force based on slope
                        playerVelocity.y += slopeFactor * 2.0f;
                    }
                }
            }
        }
    }
    
    // Debug output
    static int frameCount = 0;
    if (frameCount++ % 120 == 0) {
        std::cout << "Physics - Pos: (" << playerPos.x << ", " << playerPos.y << ", " << playerPos.z 
                  << ") Vel: (" << playerVelocity.x << ", " << playerVelocity.y << ", " << playerVelocity.z 
                  << ") - " << (onGround ? "ON GROUND" : "IN AIR/FALLING") << std::endl;
    }
    
    // Update camera
    camera.Position = glm::vec3(playerPos.x, playerPos.y + 1.6f, playerPos.z);
    camera.updateCameraVectors();
}

void Game::handleJump() {
    // Use the new getGroundAt function for rotated blocks
    bool onGround = false;

    if (currentMap) {
        float groundHeight;
        glm::vec3 groundNormal;
        bool foundGround = currentMap->getGroundAt(playerPos, 0.2f, groundHeight, groundNormal);

        if (foundGround) {
            float heightAboveGround = playerPos.y - groundHeight;
            if (heightAboveGround < 1.7f && heightAboveGround > 0.0f) {
                onGround = true;
            }
        }
    }

    if (onGround && !isJumping && jumpCooldown <= 0.0f) {
        playerVelocity.y = 4.0f; // Jump velocity
        isJumping = true;
        jumpCooldown = 0.5f; // 0.5 second cooldown
        std::cout << "Jump triggered! Player y: " << playerPos.y << std::endl;
    }
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
            return true;
        }
    }
    return false;
}

void Game::shoot() {
    if (currentWeaponInstance && currentWeaponInstance->canShoot()) {
        currentWeaponInstance->shoot(this);
    }
}

void Game::switchWeapon(int weaponNumber) {
    switch (weaponNumber) {
        case 1:
            currentWeapon = PISTOL;
            currentWeaponInstance = pistol;
            std::cout << "Switched to Pistol! Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(pistolAmmo)) << std::endl;
            break;
        case 2:
            currentWeapon = PUMP_SHOTGUN;
            currentWeaponInstance = shotgun;
            std::cout << "Switched to Pump Shotgun! Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(shotgunAmmo)) << std::endl;
            break;
        case 3:
            currentWeapon = GRENADE;
            currentWeaponInstance = grenade;
            std::cout << "Switched to Grenade! Count: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
            break;
        case 4:
            currentWeapon = FLASHBANG;
            currentWeaponInstance = flashbang;
            std::cout << "Switched to Flashbang! Count: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
            break;
    }
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
    
    createProjectile(camera.Position, camera.Front * 120.0f, 3.0f, 0, 0); // owner=0, type=0 (pistol) for local player
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
            createProjectile(camera.Position, (camera.Front + spread) * 50.0f, 1.0f, 0, 3); // owner=0, type=3 (shotgun) for local player
            pelletsShot++;
        }
    }
    if (!infiniteAmmoMode) {
        shotgunAmmo--;
    }
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
                    createSimpleExplosion(atPos);
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
                    
                    // Block bounds
                    glm::vec3 blockMin = block.position - block.scale * 0.5f;
                    glm::vec3 blockMax = block.position + block.scale * 0.5f;
                    
                    // Simple point check first (for slow grenades)
                    if (projectiles[i].position.x >= blockMin.x && projectiles[i].position.x <= blockMax.x &&
                        projectiles[i].position.y >= blockMin.y && projectiles[i].position.y <= blockMax.y &&
                        projectiles[i].position.z >= blockMin.z && projectiles[i].position.z <= blockMax.z) {
                        
                        // No bounce: detonate on impact.
                        detonateAndDeactivate(projectiles[i].position);
                        break;
                    }
                    
                    // Swept collision for fast-moving grenades - ray-box intersection
                    if (rayLength > 0.1f) {
                        // Ray-box intersection using slab method
                        float tmin = 0.0f;
                        float tmax = rayLength;
                        
                        // Check X slab
                        if (abs(rayDir.x) < 0.0001f) {
                            if (prevPos.x < blockMin.x || prevPos.x > blockMax.x) continue;
                        } else {
                            float tx1 = (blockMin.x - prevPos.x) / rayDir.x;
                            float tx2 = (blockMax.x - prevPos.x) / rayDir.x;
                            float txmin = std::min(tx1, tx2);
                            float txmax = std::max(tx1, tx2);
                            tmin = std::max(tmin, txmin);
                            tmax = std::min(tmax, txmax);
                            if (tmin > tmax) continue;
                        }
                        
                        // Check Y slab
                        if (abs(rayDir.y) < 0.0001f) {
                            if (prevPos.y < blockMin.y || prevPos.y > blockMax.y) continue;
                        } else {
                            float ty1 = (blockMin.y - prevPos.y) / rayDir.y;
                            float ty2 = (blockMax.y - prevPos.y) / rayDir.y;
                            float tymin = std::min(ty1, ty2);
                            float tymax = std::max(ty1, ty2);
                            tmin = std::max(tmin, tymin);
                            tmax = std::min(tmax, tymax);
                            if (tmin > tmax) continue;
                        }
                        
                        // Check Z slab
                        if (abs(rayDir.z) < 0.0001f) {
                            if (prevPos.z < blockMin.z || prevPos.z > blockMax.z) continue;
                        } else {
                            float tz1 = (blockMin.z - prevPos.z) / rayDir.z;
                            float tz2 = (blockMax.z - prevPos.z) / rayDir.z;
                            float tzmin = std::min(tz1, tz2);
                            float tzmax = std::max(tz1, tz2);
                            tmin = std::max(tmin, tzmin);
                            tmax = std::min(tmax, tzmax);
                            if (tmin > tmax) continue;
                        }
                        
                        // Collision detected along the ray
                        if (tmin <= rayLength && tmin >= 0.0f) {
                            // Position grenade at impact point
                            projectiles[i].position = prevPos + rayDir * (tmin - 0.01f);

                            // No bounce: detonate on impact.
                            detonateAndDeactivate(projectiles[i].position);
                            break;
                        }
                    }
                }
                
                // Store current position for next frame
                prevGrenadePositions[i] = projectiles[i].position;
            }
            
            // Simple boundary check (no bounce): treat edges/floor as walls
            float boundary = 20.0f;
            bool hitBoundary =
                projectiles[i].position.x > boundary || projectiles[i].position.x < -boundary ||
                projectiles[i].position.z > boundary || projectiles[i].position.z < -boundary ||
                projectiles[i].position.y <= -10.0f;

            if (hitBoundary) {
                // Clamp so effects/holes spawn near the boundary, not far outside it.
                glm::vec3 clamped = projectiles[i].position;
                clamped.x = std::min(boundary, std::max(-boundary, clamped.x));
                clamped.z = std::min(boundary, std::max(-boundary, clamped.z));
                clamped.y = std::max(-9.8f, clamped.y);

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
            if (checkProjectileEnemyCollision(projectiles[i]) && projectiles[i].active) {
                // Create simple explosion for grenades
                bool isGrenade = projectiles[i].type == 1;
                bool isFlashbang = projectiles[i].type == 2;

                if (isGrenade) {
                    if (!projectiles[i].detonated) {
                        projectiles[i].detonated = true;
                        projectiles[i].active = false;
                        activeProjectiles--;
                        createSimpleExplosion(projectiles[i].position);
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
            
            // Check collision with walls (different behavior for different projectile types)
            if (checkProjectileCollision(projectiles[i]) && projectiles[i].active) {
                // Check projectile type by type field
                bool isGrenade = projectiles[i].type == 1;
                bool isFlashbang = projectiles[i].type == 2;
                bool isPistolBullet = projectiles[i].type == 0;
                bool isShotgun = projectiles[i].type == 3;

                if (isPistolBullet || isShotgun) {
                    // Pistol/shotgun bullets: simple damage, no bounce, no explosion
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
                        createSimpleExplosion(projectiles[i].position);
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

void Game::createSimpleExplosion(glm::vec3 position) {
    std::cout << "BOOM! Grenade exploded at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    
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
    
    // Damage nearby enemies
    float explosionRadius = 3.0f; // 3 unit explosion radius
    float explosionDamage = 75.0f; // 75 damage
    
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (enemies[j].active && enemies[j].alive) {
            glm::vec3 enemyPos(enemies[j].x, enemies[j].y, enemies[j].z);
            float distance = glm::length(position - enemyPos);
            if (distance < explosionRadius) {
                // Damage enemy based on distance
                float damageMultiplier = 1.0f - (distance / explosionRadius);
                enemies[j].health -= explosionDamage * damageMultiplier;
                std::cout << "Enemy hit by explosion! Health: " << enemies[j].health << std::endl;
                
                if (enemies[j].health <= 0.0f) {
                    enemies[j].alive = false;
                    enemies[j].active = false;
                    activeEnemies--;
                    std::cout << "Enemy killed by explosion!" << std::endl;
                }
            }
        }
    }
    
    // Damage player if too close
    float playerDistance = glm::length(position - playerPos);
    if (playerDistance < explosionRadius && !isDead) {
        float damageMultiplier = 1.0f - (playerDistance / explosionRadius);
        playerHealth -= 50.0f * damageMultiplier; // 50 max damage for grenade
        damageEffectTime = 1.5f; // Longer screen effect
        std::cout << "Player hit by explosion! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
        
        if (playerHealth <= 0.0f && !isDead) {
            playerHealth = 0.0f;
            isDead = true;
            
            if (bombDefusalMode) {
                spectatorMode = true;
                std::cout << "Player died! Entering spectator mode." << std::endl;
            } else {
                gameState = DEAD;
            }
            std::cout << "Player defeated by explosion!" << std::endl;
        }
    }
}

void Game::createFlashbangEffect(glm::vec3 position) {
    std::cout << "FLASH! Flashbang detonated at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;

    // Calculate distance from player to flashbang
    float playerDistance = glm::length(position - playerPos);
    float flashRadius = 12.0f; // 12 unit flash radius (increased for more aggressive effect)

    // Check if there's a wall between player and flashbang (but ignore walls very close to flashbang)
    bool hasLineOfSight = true;
    if (currentMap) {
        glm::vec3 rayDir = glm::normalize(position - playerPos);
        float rayLength = playerDistance;
        const std::vector<MapBlock>& blocks = currentMap->getBlocks();

        for (const MapBlock& block : blocks) {
            if (!block.solid) continue;

            // Simple ray-box intersection check
            glm::vec3 blockMin = block.position - block.scale * 0.5f;
            glm::vec3 blockMax = block.position + block.scale * 0.5f;

            // Check if ray intersects block
            float tMin = 0.0f, tMax = rayLength;
            for (int axis = 0; axis < 3; axis++) {
                if (rayDir[axis] != 0.0f) {
                    float t1 = (blockMin[axis] - playerPos[axis]) / rayDir[axis];
                    float t2 = (blockMax[axis] - playerPos[axis]) / rayDir[axis];
                    tMin = std::max(tMin, std::min(t1, t2));
                    tMax = std::min(tMax, std::max(t1, t2));
                }
            }

            // Only block if the wall is NOT very close to the flashbang (flashbang is touching the wall)
            // If tMax is very close to rayLength, the flashbang is right next to the wall
            if (tMin < tMax && tMin < rayLength && tMax > 0.0f) {
                float distanceFromFlashbang = rayLength - tMax;
                if (distanceFromFlashbang > 0.5f) {
                    // Wall is significantly between player and flashbang
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
    glm::vec3 toFlash = position - camera.Position;
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
    // Simple boundary check - treat world edges as walls
    float boundary = 50.0f;

    // Only check boundaries if projectile has moved significantly from spawn
    float distanceFromPlayer = glm::length(projectile.position - camera.Position);
    if (distanceFromPlayer > 1.0f) { // Only check boundaries after moving 1 unit
        if (abs(projectile.position.x) >= boundary ||
            abs(projectile.position.z) >= boundary ||
            projectile.position.y <= -10.0f) {
            return true;
        }
    }

    // Check collision with map blocks if map is loaded
    if (currentMap && distanceFromPlayer > 0.5f) {
        const std::vector<MapBlock>& blocks = currentMap->getBlocks();

        // Store previous position for swept collision
        static glm::vec3 prevPositions[MAX_PROJECTILES] = {glm::vec3(0.0f)};
        static bool prevPositionsInitialized[MAX_PROJECTILES] = {false};
        int projIndex = -1;
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (&projectiles[i] == &projectile) {
                projIndex = i;
                break;
            }
        }

        glm::vec3 prevPos;
        if (projIndex >= 0 && prevPositionsInitialized[projIndex]) {
            prevPos = prevPositions[projIndex];
        } else {
            prevPos = projectile.position;
            if (projIndex >= 0) {
                prevPositionsInitialized[projIndex] = true;
            }
        }

        glm::vec3 currPos = projectile.position;
        glm::vec3 rayDir = currPos - prevPos;
        float rayLength = glm::length(rayDir);

        if (rayLength > 0.001f) {
            rayDir = rayDir / rayLength; // Normalize
        }

        // Check each block with swept collision (ray-box intersection)
        for (const MapBlock& block : blocks) {
            if (!block.solid) continue;

            // Block bounds (axis-aligned in world space)
            glm::vec3 blockMin = block.position - block.scale * 0.5f;
            glm::vec3 blockMax = block.position + block.scale * 0.5f;

            // Only check collision if projectile has traveled some distance
            if (rayLength > 0.1f) {
                // Swept collision for fast projectiles - ray-box intersection
                float tmin = 0.0f;
                float tmax = rayLength;

                // Check X slab
                if (abs(rayDir.x) < 0.0001f) {
                    if (currPos.x < blockMin.x || currPos.x > blockMax.x) continue;
                } else {
                    float t1 = (blockMin.x - prevPos.x) / rayDir.x;
                    float t2 = (blockMax.x - prevPos.x) / rayDir.x;
                    tmin = std::max(tmin, std::min(t1, t2));
                    tmax = std::min(tmax, std::max(t1, t2));
                }

                // Check Y slab
                if (abs(rayDir.y) < 0.0001f) {
                    if (currPos.y < blockMin.y || currPos.y > blockMax.y) continue;
                } else {
                    float t1 = (blockMin.y - prevPos.y) / rayDir.y;
                    float t2 = (blockMax.y - prevPos.y) / rayDir.y;
                    tmin = std::max(tmin, std::min(t1, t2));
                    tmax = std::min(tmax, std::max(t1, t2));
                }

                // Check Z slab
                if (abs(rayDir.z) < 0.0001f) {
                    if (currPos.z < blockMin.z || currPos.z > blockMax.z) continue;
                } else {
                    float t1 = (blockMin.z - prevPos.z) / rayDir.z;
                    float t2 = (blockMax.z - prevPos.z) / rayDir.z;
                    tmin = std::max(tmin, std::min(t1, t2));
                    tmax = std::min(tmax, std::max(t1, t2));
                }

                // Check if ray intersects block
                if (tmin <= tmax && tmin >= 0.0f && tmin <= rayLength) {
                    return true;
                }
            }
        }

        // Store current position for next frame (only if no collision)
        if (projIndex >= 0) {
            prevPositions[projIndex] = currPos;
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

void Game::spawnEnemy(const glm::vec3& position) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Initialize enemy with simplified struct
            enemies[i].x = position.x;
            enemies[i].y = position.y;
            enemies[i].z = position.z;
            enemies[i].health = 50.0f;
            enemies[i].active = true;
            enemies[i].alive = true;
            activeEnemies++;

            std::cout << "Spawned enemy at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
            break;
        }
    }

    if (activeEnemies >= MAX_ENEMIES) {
        std::cout << "Max enemies reached!" << std::endl;
    }
}

bool Game::checkProjectileEnemyCollision(Projectile& projectile) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Check distance between projectile and enemy
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            float distance = glm::length(projectile.position - enemyPos);
            if (distance < 0.5f) { // Hit radius of 0.5 units
                // Damage the enemy
                enemies[i].health -= 25.0f; // 25 damage per hit
                std::cout << "Enemy hit! Health: " << enemies[i].health << std::endl;
                
                if (enemies[i].health <= 0.0f) {
                    enemies[i].alive = false;
                    enemies[i].active = false;
                    activeEnemies--;
                    std::cout << "Enemy defeated!" << std::endl;
                }
                return true; // Hit detected
            }
        }
    }
    return false; // No hit
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
                // Damage player
                playerHealth -= 10.0f; // 10 damage per hit
                playerDamageCooldown = 1.0f; // 1 second cooldown
                damageEffectTime = 0.5f; // 0.5 second screen effect
                std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
                
                if (playerHealth <= 0.0f && !isDead) {
                    playerHealth = 0.0f;
                    isDead = true;
                    
                    if (bombDefusalMode) {
                        spectatorMode = true;
                        std::cout << "Player died! Entering spectator mode." << std::endl;
                    } else {
                        gameState = DEAD;
                    }
                    std::cout << "Player defeated!" << std::endl;
                }
                break; // Only one enemy can damage per frame
            }
        }
    }
}

void Game::updateEnemies(float deltaTime) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Calculate direction to player
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            glm::vec3 direction = playerPos - enemyPos;
            float distance = glm::length(direction);
            
            if (distance > 0.1f) { // Avoid division by zero
                // Normalize direction and move towards player (2.0 units per second)
                direction = glm::normalize(direction);
                glm::vec3 velocity = direction * 2.0f * deltaTime;
                enemies[i].x += velocity.x;
                enemies[i].y += velocity.y;
                enemies[i].z += velocity.z;
            }
        }
    }
}

void Game::checkProjectilePlayerCollision() {
    if (playerDamageCooldown > 0.0f) {
        return; // Player has damage immunity
    }
    
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;
        
        // Only check projectiles from other players (owner != 0 means remote player)
        if (projectiles[i].owner == 0) continue; // Skip own projectiles
        
        // Check collision with local player
        float distance = glm::length(projectiles[i].position - playerPos);
        if (distance < 0.5f) { // Hit radius of 0.5 units
            float damage = 0.0f;
            float velocity = glm::length(projectiles[i].velocity);
            if (velocity > 100.0f) {
                damage = 25.0f; // Pistol bullet (velocity ~120)
            } else if (velocity > 40.0f && velocity < 60.0f) {
                damage = 10.0f; // Shotgun pellet (velocity ~50)
            } else {
                // Grenade/flashbang explosion damage (velocity ~25-30)
                createSimpleExplosion(projectiles[i].position);
                damage = 50.0f; // Explosion damage
            }
            
            damagePlayer(damage);
            projectiles[i].active = false;
            activeProjectiles--;
            break;
        }
    }
}

void Game::damagePlayer(float damage) {
    if (isDead) return; // Already dead
    
    playerHealth -= damage;
    if (playerHealth < 0.0f) playerHealth = 0.0f;
    
    damageEffectTime = 1.0f; // Screen effect
    playerDamageCooldown = 1.0f; // 1 second immunity
    
    std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
    
    if (playerHealth <= 0.0f) {
        isDead = true;
        
        if (bombDefusalMode) {
            spectatorMode = true;
            std::cout << "Player died! Entering spectator mode." << std::endl;
        } else {
            gameState = DEAD;
        }
        std::cout << "Player defeated!" << std::endl;
    }
}

void Game::checkPlayerPlayerCollision() {
    if (playerDamageCooldown > 0.0f) {
        return; // Player has damage immunity
    }
    
    // Check collision with remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        if (!remotePlayers[i].active) continue;
        
        glm::vec3 remotePlayerPos = remotePlayers[i].position;
        float distance = glm::length(playerPos - remotePlayerPos);
        
        if (distance < 1.0f) { // Collision radius of 1 unit
            // Apply collision damage (melee damage)
            damagePlayer(15.0f); // 15 damage for player collision
        }
    }
}

// Bomb Defusal Mode Functions
void Game::updateBomb(float deltaTime) {
    if (!bombDefusalMode) return;
    
    // Update bomb timer if planted
    if (bomb.planted && !bomb.defused) {
        bomb.plantTime += deltaTime;
        
        // Check if bomb exploded
        if (bomb.plantTime >= bomb.explodeTime) {
            std::cout << "BOMB EXPLODED!" << std::endl;
            bomb.active = false;
            bomb.planted = false; // Prevent re-triggering
            endRound(bomb.plantingTeam); // Planting team wins
        }
    }
    
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
                std::cout << "BOMB DEFUSED!" << std::endl;
                endRound(1 - bomb.plantingTeam); // Defusing team wins
            }
        }
    }
}

void Game::startPlanting() {
    if (!bombDefusalMode || bomb.planted || isPlanting) return;
    
    // Only attacking team can plant
    if (playerTeam != attackingTeam) {
        std::cout << "Only attacking team can plant the bomb!" << std::endl;
        return;
    }
    
    int siteIndex = getNearbyBombSite();
    if (siteIndex >= 0) {
        isPlanting = true;
        plantProgress = 0.0f;
        std::cout << "Started planting bomb..." << std::endl;
    }
}

void Game::startDefusing() {
    if (!bombDefusalMode || !bomb.planted || bomb.defused || isDefusing) return;
    
    // Only defending team can defuse
    if (playerTeam == bomb.plantingTeam) {
        std::cout << "Cannot defuse your own bomb!" << std::endl;
        return;
    }
    
    if (isNearBomb()) {
        isDefusing = true;
        defuseProgress = 0.0f;
        std::cout << "Started defusing bomb..." << std::endl;
    }
}

int Game::getNearbyBombSite() {
    for (int i = 0; i < activeBombSites; i++) {
        if (!bombSites[i].active) continue;
        
        float distance = glm::length(playerPos - bombSites[i].position);
        if (distance < 2.0f) { // Within 2 units of bomb site
            return i;
        }
    }
    return -1;
}

bool Game::isNearBomb() {
    if (!bomb.planted || bomb.defused) return false;
    
    float distance = glm::length(playerPos - bomb.position);
    return distance < 2.0f; // Within 2 units of bomb
}

void Game::endRound(int winner) {
    // Prevent re-entry if round already ended
    if (roundEndTimer > 0.0f) return;
    
    roundActive = false;
    roundEndTimer = 4.0f; // 4 second delay before next round
    
    if (winner == 0) {
        std::cout << "ROUND OVER: Team A (Red) wins!" << std::endl;
    } else if (winner == 1) {
        std::cout << "ROUND OVER: Team B (Blue) wins!" << std::endl;
    } else {
        std::cout << "ROUND OVER: Draw!" << std::endl;
    }
    
    // Swap attacking team for next round
    attackingTeam = 1 - attackingTeam;
    
    // Create a big explosion at the bomb position if bomb was planted
    if (bomb.planted || bomb.active) {
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
    
    // Also damage/kill the player if near the bomb explosion
    if (bomb.planted || bomb.active) {
        float distToBomb = glm::length(playerPos - bomb.position);
        if (distToBomb < 12.0f) {
            float damage = (12.0f - distToBomb) / 12.0f * 100.0f;
            if (damage > 0.0f) {
                damagePlayer(damage);
                if (playerHealth <= 0.0f) {
                    playerHealth = 0.0f;
                    isDead = true;
                    if (bombDefusalMode) {
                        spectatorMode = true;
                    } else {
                        gameState = DEAD;
                    }
                }
            }
        }
    }
}

void Game::updateRoundEndTimer(float deltaTime) {
    if (roundEndTimer > 0.0f) {
        roundEndTimer -= deltaTime;
        if (roundEndTimer <= 0.0f) {
            startRound();
        }
    }
}

void Game::startRound() {
    roundActive = true;
    roundTime = 0.0f;
    teamADeaths = 0;
    teamBDeaths = 0;
    
    // Reset bomb
    bomb.planted = false;
    bomb.defused = false;
    bomb.active = false;
    bomb.plantTime = 0.0f;
    
    // Reset players
    resetGame();
    
    std::cout << "NEW ROUND STARTED! Attacking Team: " << (attackingTeam == 0 ? "Team A (Red)" : "Team B (Blue)") << std::endl;
}

void Game::updateSpectator() {
    if (!spectatorMode) return;
    
    // Simple free camera for spectator
    // (In a full implementation, this would cycle through living teammates)
    float spectatorSpeed = 10.0f;
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        playerPos += camera.Front * spectatorSpeed * 0.016f;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        playerPos -= camera.Front * spectatorSpeed * 0.016f;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        playerPos -= camera.Right * spectatorSpeed * 0.016f;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        playerPos += camera.Right * spectatorSpeed * 0.016f;
    }
}