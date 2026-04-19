#include "physics.hpp"
#include <iostream>

Physics::Physics() : gravity(9.8f), jumpVelocity(4.0f) {
}

void Physics::updatePlayerPhysics(float deltaTime, Map* map) {
    // AUTOMATIC UNSTUCK: Check if player is inside a block and move them out
    unstuckPlayer(map);
    
    // Update jump cooldown
    if (player.jumpCooldown > 0.0f) {
        player.jumpCooldown -= deltaTime;
    }
    
    // Apply gravity
    player.velocity.y -= gravity * deltaTime;
    
    // Calculate intended new position
    glm::vec3 newPos = player.position + player.velocity * deltaTime;
    
    // Store original position
    glm::vec3 originalPos = player.position;
    
    // Check collision at new position
    bool collision = false;
    if (map) {
        collision = checkCollision(newPos, player.collisionRadius, map);
    }
    
    if (collision) {
        // Try X axis movement only
        glm::vec3 testPosX = originalPos;
        testPosX.x = newPos.x;
        bool xCollision = map ? checkCollision(testPosX, player.collisionRadius, map) : false;
        
        // Try Z axis movement only  
        glm::vec3 testPosZ = originalPos;
        testPosZ.z = newPos.z;
        bool zCollision = map ? checkCollision(testPosZ, player.collisionRadius, map) : false;
        
        // Try Y axis movement only (for gravity)
        glm::vec3 testPosY = originalPos;
        testPosY.y = newPos.y;
        bool yCollision = map ? checkCollision(testPosY, player.collisionRadius, map) : false;
        
        // Apply movement only on axes without collision
        if (!xCollision) {
            player.position.x = newPos.x;
        } else {
            player.velocity.x = 0.0f;
        }
        
        if (!zCollision) {
            player.position.z = newPos.z;
        } else {
            player.velocity.z = 0.0f;
        }
        
        if (!yCollision) {
            player.position.y = newPos.y;
        } else {
            // Check if we're hitting a floor (moving down and blocked)
            if (player.velocity.y < 0.0f) {
                player.velocity.y = 0.0f;
                player.isJumping = false;
                // Ensure player is on top of the block
                player.position.y = originalPos.y;
            } else if (player.velocity.y > 0.0f) {
                // Hitting ceiling
                player.velocity.y = 0.0f;
                player.position.y = originalPos.y;
            }
        }
    } else {
        // No collision - allow full movement
        player.position = newPos;
    }
    
    // Debug output
    static int debugCounter = 0;
    if (++debugCounter % 60 == 0) { // Every 60 frames
        std::cout << "Physics - Pos: (" << player.position.x << ", " << player.position.y << ", " << player.position.z 
                  << ") Vel: (" << player.velocity.x << ", " << player.velocity.y << ", " << player.velocity.z 
                  << ") - " << (isOnGround() ? "ON GROUND" : "IN AIR") << std::endl;
    }
}

void Physics::handleJump() {
    // Only allow jump if on ground and not already jumping
    if (isOnGround() && !player.isJumping && player.jumpCooldown <= 0.0f) {
        player.velocity.y = jumpVelocity;
        player.isJumping = true;
        player.jumpCooldown = 0.5f; // 0.5 second cooldown
        std::cout << "Jump triggered! Player y: " << player.position.y << std::endl;
    }
}

void Physics::handleJump(Map* map) {
    // Only allow jump if on ground and not already jumping
    if (isOnGround(map) && !player.isJumping && player.jumpCooldown <= 0.0f) {
        player.velocity.y = jumpVelocity;
        player.isJumping = true;
        player.jumpCooldown = 0.5f; // 0.5 second cooldown
        std::cout << "Jump triggered! Player y: " << player.position.y << std::endl;
    }
}

bool Physics::isOnGround() const {
    // Check if player is on or very close to ground (y=0)
    bool onGroundLevel = player.position.y <= 0.1f && player.velocity.y >= -0.1f;
    
    // Check if there's a block beneath the player
    glm::vec3 belowPlayer = player.position;
    belowPlayer.y -= 0.2f; // Check slightly below player
    
    // Check if any of these points would collide with a block
    // This is a simplified check - in a full implementation we'd pass map to this function
    // For now, just use ground level detection
    return onGroundLevel;
}

bool Physics::isOnGround(Map* map) const {
    // Check if player is on or very close to ground (y=0)
    bool onGroundLevel = player.position.y <= 0.1f && player.velocity.y >= -0.1f;
    
    if (map) {
        // Check if player is on top of a block
        if (const_cast<Physics*>(this)->isOnTopOfBlock(player.position, map)) {
            return true;
        }
    }
    
    return onGroundLevel;
}

void Physics::applyMovement(const glm::vec3& movement, float deltaTime) {
    player.velocity = movement;
}

bool Physics::checkCollision(const glm::vec3& position, float radius, Map* map) {
    if (!map) return false;
    
    // Simple boundary check
    float boundary = 20.0f;
    if (abs(position.x) >= boundary || 
        abs(position.z) >= boundary ||
        position.y <= -10.0f) {
        return true;
    }
    
    // Check collision with map blocks
    const auto& blocks = map->getBlocks();
    for (const auto& block : blocks) {
        if (!block.solid) continue;
        
        // Simple AABB-sphere collision
        glm::vec3 blockMin = block.position - block.scale * 0.5f;
        glm::vec3 blockMax = block.position + block.scale * 0.5f;
        
        // Find closest point on box to sphere center
        glm::vec3 closestPoint = glm::clamp(position, blockMin, blockMax);
        
        // Check if closest point is within sphere radius
        float distance = glm::length(position - closestPoint);
        if (distance < radius) {
            return true;
        }
    }
    
    return false;
}

// Helper function to check if position is on top of a block
bool Physics::isOnTopOfBlock(const glm::vec3& position, Map* map) {
    if (!map) return false;
    
    const auto& blocks = map->getBlocks();
    for (const auto& block : blocks) {
        if (!block.solid) continue;
        
        // Check if player is directly above this block
        glm::vec3 blockMin = block.position - block.scale * 0.5f;
        glm::vec3 blockMax = block.position + block.scale * 0.5f;
        
        // Check if player is within block's X/Z bounds
        if (position.x >= blockMin.x && position.x <= blockMax.x &&
            position.z >= blockMin.z && position.z <= blockMax.z) {
            
            // Check if player is very close to the top of the block
            float blockTop = blockMax.y;
            if (abs(position.y - blockTop) < 0.2f && position.y >= blockTop - 0.1f) {
                return true;
            }
        }
    }
    
    return false;
}

void Physics::unstuckPlayer(Map* map) {
    if (map && checkCollision(player.position, player.collisionRadius, map)) {
        int unstuckCounter = 0;
        while (checkCollision(player.position, player.collisionRadius, map) && unstuckCounter < 50) {
            player.position.y += 0.2f; // Move up gradually
            unstuckCounter++;
        }
        if (unstuckCounter > 0) {
            std::cout << "UNSTUCK: Moved player up by " << (unstuckCounter * 0.2f) << " units" << std::endl;
            player.velocity = glm::vec3(0.0f, 0.0f, 0.0f); // Reset velocity
        }
    }
}
