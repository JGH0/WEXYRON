#ifndef GAME_PROJECTILE_HPP
#define GAME_PROJECTILE_HPP

#include <glm/glm.hpp>

/**
 * @brief Projectile system for the game
 */
class ProjectileSystem {
public:
    struct Projectile {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime;
        bool active;
    };

private:
    static const int MAX_PROJECTILES = 100;
    Projectile projectiles[MAX_PROJECTILES];
    int activeProjectiles;

public:
    ProjectileSystem();
    
    // Projectile management
    bool createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime);
    void updateProjectiles(float deltaTime, class Map* map);
    void renderProjectiles(class Renderer* renderer);
    
    // Getters
    int getActiveProjectiles() const { return activeProjectiles; }
    const Projectile* getProjectiles() const { return projectiles; }
    int getMaxProjectiles() const { return MAX_PROJECTILES; }
    
    // Cleanup
    void clearAllProjectiles();
};

#endif
