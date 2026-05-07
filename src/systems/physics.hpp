#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include <glm/glm.hpp>
#include "../core/map.hpp"

/**
 * @brief Physics system for player movement and collision detection
 */
class Physics {
public:
    struct PlayerState {
        glm::vec3 position;
        glm::vec3 velocity;
        float collisionRadius;
        bool isJumping;
        float jumpCooldown;
        
        PlayerState() : position(0.0f), velocity(0.0f), collisionRadius(0.3f), 
                       isJumping(false), jumpCooldown(0.0f) {}
    };

private:
    PlayerState player;
    float gravity;
    float jumpVelocity;

public:
    Physics();
    
    // Player physics
    void updatePlayerPhysics(float deltaTime, Map* map);
    void handleJump();
    void handleJump(Map* map);
    void applyMovement(const glm::vec3& movement, float deltaTime);
    
    // Getters
    const PlayerState& getPlayerState() const { return player; }
    glm::vec3 getPlayerPosition() const { return player.position; }
    glm::vec3 getPlayerVelocity() const { return player.velocity; }
    bool isOnGround() const;
    bool isOnGround(Map* map) const;
    
    // Setters
    void setPlayerPosition(const glm::vec3& pos) { player.position = pos; }
    void setPlayerVelocity(const glm::vec3& vel) { player.velocity = vel; }
    void resetJumpState() { player.isJumping = false; player.jumpCooldown = 0.0f; }
    
    // Collision detection
    bool checkCollision(const glm::vec3& position, float radius, Map* map);
    bool isOnTopOfBlock(const glm::vec3& position, Map* map);
    void unstuckPlayer(Map* map);
};

#endif
